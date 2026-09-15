// SPDX-License-Identifier: GPL-3.0-or-later
using System.Text;
using System.Text.Json.Nodes;
using TaskbarStyler.Tray.Core;

var tests = new (string Name, Action Run)[]
{
    ("failed load is not retried by polling", () =>
    {
        var machine = new TrayStateMachine();
        var request = machine.BeginOperation(Shell.One, OperationTrigger.Startup, true, false)!;
        Check.Equal(OperationAction.Load, request.Action);
        Check.True(machine.Complete(request.Id, false, "0x80070490"));
        Check.Equal(TrayState.Failed, machine.State);
        Check.Null(machine.BeginOperation(Shell.One, OperationTrigger.Poll, true, false));
        Check.Equal("0x80070490", machine.LastError);
    }),
    ("explicit action and TaskbarCreated permit retry", () =>
    {
        foreach (var trigger in new[] { OperationTrigger.UserAction, OperationTrigger.TaskbarCreated })
        {
            var machine = new TrayStateMachine();
            var first = machine.BeginOperation(Shell.One, OperationTrigger.Startup, true, false)!;
            machine.Complete(first.Id, false, "failed");
            Check.Equal(OperationAction.Load, machine.BeginOperation(Shell.One, trigger, true, false)!.Action);
        }
    }),
    ("new process creation permits retry even when PID reused", () =>
    {
        var machine = new TrayStateMachine();
        var first = machine.BeginOperation(Shell.One, OperationTrigger.Startup, true, false)!;
        machine.Complete(first.Id, false, "failed");
        Check.Equal(OperationAction.Load,
            machine.BeginOperation(Shell.ReusedPid, OperationTrigger.Poll, true, false)!.Action);
    }),
    ("stale completion cannot overwrite new generation", () =>
    {
        var machine = new TrayStateMachine();
        var first = machine.BeginOperation(Shell.One, OperationTrigger.Startup, true, false)!;
        var second = machine.BeginOperation(Shell.Two, OperationTrigger.TaskbarCreated, true, false)!;
        Check.False(machine.Complete(first.Id, false, "old failure"));
        Check.True(machine.Complete(second.Id, true));
        Check.Equal(TrayState.Active, machine.State);
        Check.Null(machine.LastError);
    }),
    ("new user intent supersedes old same-process completion", () =>
    {
        var machine = new TrayStateMachine();
        var apply = machine.BeginOperation(Shell.One, OperationTrigger.Startup, true, false)!;
        var reset = machine.BeginOperation(Shell.One, OperationTrigger.UserAction, false, true)!;
        Check.Equal(OperationAction.Reset, reset.Action);
        Check.False(machine.Complete(apply.Id, true));
        Check.True(machine.Complete(reset.Id, true));
        Check.Equal(TrayState.Inactive, machine.State);
    }),
    ("reset with no TAP does not load it", () =>
    {
        var machine = new TrayStateMachine();
        Check.Null(machine.BeginOperation(Shell.One, OperationTrigger.UserAction, false, false));
        Check.Equal(TrayState.Inactive, machine.State);
    }),
    ("missing shell waits and invalidates pending completion", () =>
    {
        var machine = new TrayStateMachine();
        var request = machine.BeginOperation(Shell.One, OperationTrigger.Startup, true, false)!;
        Check.Null(machine.BeginOperation(null, OperationTrigger.Poll, true, false));
        Check.Equal(TrayState.Waiting, machine.State);
        Check.False(machine.Complete(request.Id, true));
    }),
    ("poll neither re-signals an active TAP nor interrupts work", () =>
    {
        var machine = new TrayStateMachine();
        var request = machine.BeginOperation(Shell.One, OperationTrigger.Startup, true, false)!;
        Check.Null(machine.BeginOperation(Shell.One, OperationTrigger.Poll, true, false));
        Check.True(machine.Complete(request.Id, true));
        Check.Null(machine.BeginOperation(Shell.One, OperationTrigger.Poll, true, true));
        Check.Null(machine.BeginOperation(Shell.One, OperationTrigger.Poll, true, false));
        Check.Equal(TrayState.Failed, machine.State);
        Check.Null(machine.BeginOperation(Shell.One, OperationTrigger.Poll, true, false));
    }),
    ("resident startup signals reload and completion is consumed once", () =>
    {
        var machine = new TrayStateMachine();
        var request = machine.BeginOperation(Shell.One, OperationTrigger.Startup, true, true)!;
        Check.Equal(OperationAction.Reload, request.Action);
        Check.True(machine.Complete(request.Id, true));
        Check.False(machine.Complete(request.Id, false, "duplicate"));
        Check.Equal(TrayState.Active, machine.State);
    }),
    ("validation failure cancels pending completion and blocks polling", () =>
    {
        var machine = new TrayStateMachine();
        var request = machine.BeginOperation(Shell.One, OperationTrigger.Startup, true, false)!;
        machine.Fail("Configuration is invalid.");
        Check.False(machine.IsBusy);
        Check.Equal("Configuration is invalid.", machine.LastError);
        Check.False(machine.Complete(request.Id, true));
        Check.Null(machine.BeginOperation(Shell.One, OperationTrigger.Poll, true, false));
        Check.Equal(OperationAction.Load, machine.BeginOperation(Shell.One, OperationTrigger.UserAction, true, false)!.Action);
    }),
    ("export load failure is bound to its new Explorer generation", () =>
    {
        var machine = new TrayStateMachine();
        var first = machine.BeginOperation(Shell.One, OperationTrigger.Startup, true, false)!;
        machine.Complete(first.Id, true);
        var export = machine.BeginExport(Shell.Two, true);
        Check.Equal(OperationAction.Export, export.Action);
        Check.Equal(Shell.Two, machine.CurrentExplorer!.Value);
        machine.Complete(export.Id, false, "Export load failed.");
        Check.Null(machine.BeginOperation(Shell.Two, OperationTrigger.Poll, true, false));
        Check.Equal(TrayState.Failed, machine.State);
        Check.Equal(OperationAction.Load, machine.BeginOperation(Shell.Two, OperationTrigger.UserAction, true, false)!.Action);
    }),
    ("successful export preserves configured active or inactive state", () =>
    {
        foreach (bool enabled in new[] { false, true })
        {
            var machine = new TrayStateMachine();
            var export = machine.BeginExport(Shell.One, enabled);
            Check.True(machine.IsBusy);
            Check.True(machine.Complete(export.Id, true));
            Check.Equal(enabled ? TrayState.Active : TrayState.Inactive, machine.State);
        }
    }),
    ("stale export completion cannot overwrite a newer shell or reset", () =>
    {
        var machine = new TrayStateMachine();
        var export = machine.BeginExport(Shell.One, true);
        var reset = machine.BeginOperation(Shell.Two, OperationTrigger.TaskbarCreated, false, true)!;
        Check.False(machine.Complete(export.Id, true));
        Check.True(machine.Complete(reset.Id, true));
        Check.Equal(TrayState.Inactive, machine.State);
    }),
    ("configuration preserves unknown nested values and log level", () => TempDirectory.Run(dir =>
    {
        var path = Path.Combine(dir, "config.json");
        File.WriteAllText(path, "{\"theme\":\"Old\",\"logLevel\":\"debug\",\"future\":{\"label\":\"ação\",\"values\":[1,true,null]}}");
        var store = new ConfigStore(path);
        store.SaveTheme("New_Theme");
        var actual = JsonNode.Parse(File.ReadAllText(path))!;
        Check.Equal("New_Theme", actual["theme"]!.GetValue<string>());
        Check.Equal("debug", actual["logLevel"]!.GetValue<string>());
        Check.Equal("ação", actual["future"]!["label"]!.GetValue<string>());
        Check.Equal(3, actual["future"]!["values"]!.AsArray().Count);
        Check.Equal("New_Theme", store.Read().Theme);
        store.SaveTheme("");
        Check.Equal("", store.Read().Theme);
        Check.Equal("debug", store.Read().LogLevel);
    })),
    ("first config uses existing defaults and writes UTF-8", () => TempDirectory.Run(dir =>
    {
        var path = Path.Combine(dir, "nested", "config.json");
        var store = new ConfigStore(path);
        Check.Equal("", store.Read().Theme);
        Check.Equal("", store.Read().LogLevel);
        store.SaveTheme("Pills");
        var bytes = File.ReadAllBytes(path);
        Check.False(bytes.AsSpan().StartsWith(new byte[] { 0xef, 0xbb, 0xbf }));
        Check.Equal("Pills", store.Read().Theme);
        Check.Equal(0, Directory.GetFiles(Path.GetDirectoryName(path)!, "*.tmp").Length);
    })),
    ("invalid config is never overwritten", () => TempDirectory.Run(dir =>
    {
        var path = Path.Combine(dir, "config.json");
        foreach (var invalid in new[] { "{", "[]", "{\"theme\":null}", "{\"theme\":7}", "{\"logLevel\":false}" })
        {
            File.WriteAllText(path, invalid);
            Check.Throws(() => new ConfigStore(path).SaveTheme("Pills"));
            Check.Equal(invalid, File.ReadAllText(path));
        }
    })),
    ("invalid UTF-8 config is never replaced", () => TempDirectory.Run(dir =>
    {
        var path = Path.Combine(dir, "config.json");
        byte[] invalid = [0x7b, 0x22, 0x78, 0x22, 0x3a, 0x22, 0xff, 0x22, 0x7d];
        File.WriteAllBytes(path, invalid);
        Check.Throws(() => new ConfigStore(path).SaveTheme("Pills"));
        Check.True(invalid.SequenceEqual(File.ReadAllBytes(path)));
    })),
    ("config I/O failure preserves prior bytes", () => TempDirectory.Run(dir =>
    {
        var path = Path.Combine(dir, "config.json");
        File.WriteAllText(path, "{\"theme\":\"Old\",\"logLevel\":\"debug\"}");
        var before = File.ReadAllBytes(path);
        using (var held = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
            Check.Throws(() => new ConfigStore(path).SaveTheme("Pills"));
        Check.True(before.SequenceEqual(File.ReadAllBytes(path)));
        Check.Equal(0, Directory.GetFiles(dir, "*.tmp").Length);
    })),
    ("theme ID boundary matches native allowed set", () =>
    {
        foreach (var valid in new[] { "Pills", "A&B-1.0", new string('a', 128) })
            Check.True(ThemeCatalog.IsValidId(valid));
        foreach (var invalid in new[] { "", "..", "a..b", "../Pills", "C:\\Pills", "a/b", "ação", new string('a', 129) })
            Check.False(ThemeCatalog.IsValidId(invalid));
    }),
    ("invalid requested ID leaves config untouched", () => TempDirectory.Run(dir =>
    {
        var path = Path.Combine(dir, "config.json");
        File.WriteAllText(path, "{\"theme\":\"Old\"}");
        var before = File.ReadAllText(path);
        Check.Throws(() => new ConfigStore(path).SaveTheme("../outside"));
        Check.Equal(before, File.ReadAllText(path));
    })),
    ("catalog validates metadata and filename without interpreting rules", () => TempDirectory.Run(dir =>
    {
        File.WriteAllText(Path.Combine(dir, "Good.json"), "{\"id\":\"Good\",\"name\":\"Ação\",\"author\":\"\",\"rules\":[{\"futureOpaque\":true}]}");
        File.WriteAllText(Path.Combine(dir, "Other.json"), "{\"id\":\"../Outside\",\"name\":\"Wrong\",\"rules\":[]}");
        File.WriteAllText(Path.Combine(dir, "BadAuthor.json"), "{\"id\":\"BadAuthor\",\"name\":\"Wrong\",\"author\":1,\"rules\":[]}");
        File.WriteAllText(Path.Combine(dir, "credits.json"), "not a theme");
        var catalog = ThemeCatalog.Load(dir);
        Check.Equal(1, catalog.Themes.Count);
        Check.Equal("Good", catalog.Themes[0].Id);
        Check.Equal("Ação", catalog.Themes[0].Name);
        Check.Equal(2, catalog.Errors.Count);
    })),
    ("catalog reports missing directory", () => TempDirectory.Run(dir =>
    {
        var result = ThemeCatalog.Load(Path.Combine(dir, "missing"));
        Check.Equal(0, result.Themes.Count);
        Check.Equal(1, result.Errors.Count);
    })),
    ("catalog config ID follows filename when upstream display ID differs", () => TempDirectory.Run(dir =>
    {
        File.WriteAllText(Path.Combine(dir, "A_B.json"), "{\"id\":\"A&B\",\"name\":\"A & B\",\"rules\":[]}");
        var result = ThemeCatalog.Load(dir);
        Check.Equal(0, result.Errors.Count);
        Check.Equal("A_B", result.Themes[0].Id);
        Check.Equal("A & B", result.Themes[0].Name);
    })),
    ("valid variant references hide only auxiliary menu entries", () => TempDirectory.Run(dir =>
    {
        File.WriteAllText(Path.Combine(dir, "Base.json"), "{\"id\":\"Base\",\"name\":\"Base\",\"rules\":[],\"osFeatureVariant\":{\"featureId\":42,\"themeId\":\"Alternate\"}}");
        File.WriteAllText(Path.Combine(dir, "Alternate.json"), "{\"id\":\"Alternate\",\"name\":\"Alternate\",\"rules\":[]}");
        var result = ThemeCatalog.Load(dir);
        Check.Equal(2, result.Themes.Count);
        Check.Equal(0, result.Errors.Count);
        Check.False(result.Themes.Single(theme => theme.Id == "Base").IsVariant);
        Check.True(result.Themes.Single(theme => theme.Id == "Alternate").IsVariant);
        var config = new ConfigStore(Path.Combine(dir, "config.json"));
        config.SaveTheme("Alternate");
        Check.Equal("Alternate", config.Read().Theme);
    })),
    ("malformed variant metadata reports one file without hiding valid themes", () => TempDirectory.Run(dir =>
    {
        File.WriteAllText(Path.Combine(dir, "Good.json"), "{\"id\":\"Good\",\"name\":\"Good\",\"rules\":[]}");
        File.WriteAllText(Path.Combine(dir, "Bad.json"), "{\"id\":\"Bad\",\"name\":\"Bad\",\"rules\":[],\"osFeatureVariant\":{\"featureId\":\"42\",\"themeId\":\"Good\"}}");
        var result = ThemeCatalog.Load(dir);
        Check.Equal(1, result.Themes.Count);
        Check.False(result.Themes[0].IsVariant);
        Check.Equal(1, result.Errors.Count);
    })),
    ("repository theme corpus is represented without metadata failures", () =>
    {
        var directory = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../../themes"));
        var catalog = ThemeCatalog.Load(directory);
        foreach (var error in catalog.Errors) Console.WriteLine(error);
        Check.Equal(0, catalog.Errors.Count);
        int fileCount = Directory.GetFiles(directory, "*.json").Count(path => Path.GetFileName(path) != "credits.json");
        Check.Equal(fileCount, catalog.Themes.Count);
        Console.WriteLine($"Corpus: {catalog.Themes.Count} themes, {catalog.Themes.Count(theme => !theme.IsVariant)} selectable");
    }),
    ("diagnostic matches both process identifiers and counts", () =>
    {
        var observation = DiagnosticLogReader.ParseLatest(Shell.Metric(Shell.One, 7, true, 2) + "\n", Shell.One, Shell.Now)!;
        Check.Equal(7UL, observation.Observed);
        Check.Equal(2UL, observation.Residual);
        Check.True(observation.Incomplete);
        Check.Equal("12:34:56.789", observation.LogTime);
        Check.Equal(Shell.Now, observation.ReadAt);
        Check.Equal(Shell.Now, observation.ObservedAt);
    }),
    ("old PID, reused PID and incomplete line are unavailable", () =>
    {
        Check.Null(DiagnosticLogReader.ParseLatest(Shell.Metric(Shell.Two, 0, false, 0) + "\n", Shell.One, Shell.Now));
        Check.Null(DiagnosticLogReader.ParseLatest(Shell.Metric(Shell.ReusedPid, 0, false, 0) + "\n", Shell.One, Shell.Now));
        Check.Null(DiagnosticLogReader.ParseLatest(Shell.Metric(Shell.One, 0, false, 0), Shell.One, Shell.Now));
        Check.Null(DiagnosticLogReader.ParseLatest("", Shell.One, Shell.Now));
    }),
    ("malformed metrics never become a zero observation", () =>
    {
        var valid = Shell.Metric(Shell.One, 7, true, 2);
        foreach (var invalid in new[] { valid.Replace("observed=7", "observed=-1"), valid.Replace("incomplete=1", "incomplete=2"), valid.Replace("residual=2", "residual=9"), valid.Replace("/9]", "/not-a-thread]"), valid.Replace("created=100", "created=oops") })
            Check.Null(DiagnosticLogReader.ParseLatest(invalid + "\n", Shell.One, Shell.Now));
    }),
    ("last complete matching line wins over partial data", () =>
    {
        var log = Shell.Metric(Shell.One, 7, false, 0) + "\r\n" + Shell.Metric(Shell.One, 8, false, 0) + "\n" + Shell.Metric(Shell.One, 99, false, 0);
        Check.Equal(8UL, DiagnosticLogReader.ParseLatest(log, Shell.One, Shell.Now)!.Observed);
    }),
    ("metric timestamp must belong to current process and not the future", () =>
    {
        var line = Shell.Metric(Shell.One, 7, false, 0);
        var timestamp = Shell.Now.UtcDateTime.ToFileTimeUtc().ToString();
        foreach (var invalid in new[] { "0", "99", "18446744073709551615", Shell.Now.AddMinutes(1).UtcDateTime.ToFileTimeUtc().ToString() })
            Check.Null(DiagnosticLogReader.ParseLatest(line.Replace("utc=" + timestamp, "utc=" + invalid) + "\n", Shell.One, Shell.Now));
        var old = Shell.Now.AddDays(-1);
        var observation = DiagnosticLogReader.ParseLatest(line.Replace("utc=" + timestamp, "utc=" + old.UtcDateTime.ToFileTimeUtc()) + "\n", Shell.One, Shell.Now)!;
        Check.Equal(old, observation.ObservedAt);
    }),
    ("rotation fallback is read without inventing a new observation", () => TempDirectory.Run(dir =>
    {
        var path = Path.Combine(dir, "log.txt");
        File.WriteAllText(path + ".1", Shell.Metric(Shell.One, 12, false, 1) + "\n");
        File.WriteAllText(path, "partial line");
        Check.Equal(12UL, DiagnosticLogReader.ReadLatest(path, Shell.One)!.Observed);
        File.WriteAllText(path, Shell.Metric(Shell.One, 3, false, 0) + "\n");
        Check.Equal(3UL, DiagnosticLogReader.ReadLatest(path, Shell.One)!.Observed);
        Check.Null(DiagnosticLogReader.ReadLatest(Path.Combine(dir, "missing"), Shell.One));
    })),
    ("partial UTF-8 trailing record preserves prior complete observation", () => TempDirectory.Run(dir =>
    {
        var path = Path.Combine(dir, "log.txt");
        byte[] complete = Encoding.UTF8.GetBytes(Shell.Metric(Shell.One, 4, false, 0) + "\n");
        File.WriteAllBytes(path, [.. complete, 0xc3]);
        Check.Equal(4UL, DiagnosticLogReader.ReadLatest(path, Shell.One)!.Observed);
    }))
};

var failed = 0;
foreach (var (name, run) in tests)
{
    try { run(); Console.WriteLine($"PASS {name}"); }
    catch (Exception ex) { failed++; Console.Error.WriteLine($"FAIL {name}: {ex}"); }
}
Console.WriteLine($"{tests.Length - failed}/{tests.Length} tests passed");
return failed == 0 ? 0 : 1;

static class Shell
{
    public static readonly ExplorerIdentity One = new(10, 100);
    public static readonly ExplorerIdentity Two = new(20, 200);
    public static readonly ExplorerIdentity ReusedPid = new(10, 300);
    public static readonly DateTimeOffset Now = new(2026, 9, 14, 15, 0, 0, TimeSpan.Zero);
    public static string Metric(ExplorerIdentity identity, ulong count, bool incomplete, ulong residual) =>
        $"[12:34:56.789 {identity.ProcessId}/9] INFO diagnostics handles: pid={identity.ProcessId} created={identity.CreationTimeUtcFileTime} observed={count} incomplete={(incomplete ? 1 : 0)} residual={residual} utc={Now.UtcDateTime.ToFileTimeUtc()}";
}

static class Check
{
    public static void True(bool value) { if (!value) throw new Exception("Expected true"); }
    public static void False(bool value) => True(!value);
    public static void Null(object? value) { if (value is not null) throw new Exception($"Expected null, got {value}"); }
    public static void Equal<T>(T expected, T actual) { if (!EqualityComparer<T>.Default.Equals(expected, actual)) throw new Exception($"Expected {expected}, got {actual}"); }
    public static void Throws(Action action)
    {
        try { action(); } catch { return; }
        throw new Exception("Expected exception");
    }
}

static class TempDirectory
{
    public static void Run(Action<string> action)
    {
        var root = Path.GetFullPath(Path.Combine(Path.GetTempPath(), "TaskbarStyler.Tests"));
        var path = Path.GetFullPath(Path.Combine(root, Guid.NewGuid().ToString("N")));
        if (!path.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Test directory escaped its temporary root.");
        Directory.CreateDirectory(path);
        try { action(path); }
        finally { Directory.Delete(path, true); }
    }
}
