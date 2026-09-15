// SPDX-License-Identifier: GPL-3.0-or-later
namespace TaskbarStyler.Tray.Core;

public readonly record struct ExplorerIdentity(int ProcessId, long CreationTimeUtcFileTime);
public enum TrayState { Inactive, Waiting, Active, Failed }
public enum OperationTrigger { Startup, Poll, TaskbarCreated, UserAction }
public enum OperationAction { Load, Reload, Reset, Export }
public sealed record OperationRequest(long Id, ExplorerIdentity Explorer, OperationAction Action);

// Called serially by the UI. Active means a request was accepted; the TAP has
// no acknowledgement channel and may still report individual failures in its log.
public sealed class TrayStateMachine
{
    private long nextId;
    private OperationRequest? pending;
    private bool attemptedInGeneration;
    private bool pendingThemeEnabled;

    public TrayState State { get; private set; } = TrayState.Inactive;
    public string? LastError { get; private set; }
    public ExplorerIdentity? CurrentExplorer { get; private set; }
    public bool IsBusy => pending is not null;

    public OperationRequest? BeginOperation(ExplorerIdentity? explorer,
        OperationTrigger trigger, bool themeEnabled, bool tapPresent)
    {
        if (explorer is { } identity &&
            (identity.ProcessId <= 0 || identity.CreationTimeUtcFileTime <= 0))
            throw new ArgumentException("Explorer identity is invalid.", nameof(explorer));

        bool changed = CurrentExplorer != explorer;
        if (changed || trigger == OperationTrigger.TaskbarCreated)
        {
            pending = null;
            attemptedInGeneration = false;
            CurrentExplorer = explorer;
            LastError = null;
            State = themeEnabled ? TrayState.Waiting : TrayState.Inactive;
        }

        if (explorer is null)
        {
            pending = null;
            State = themeEnabled ? TrayState.Waiting : TrayState.Inactive;
            LastError = null;
            return null;
        }

        if (trigger == OperationTrigger.Poll)
        {
            if (IsBusy || State == TrayState.Failed)
                return null;
            if (themeEnabled && tapPresent && State == TrayState.Active)
                return null;
            if (!themeEnabled && State == TrayState.Inactive && !changed)
                return null;
            if (themeEnabled && !tapPresent && attemptedInGeneration)
            {
                State = TrayState.Failed;
                LastError = "O canal do TAP está indisponível após a tentativa de carga. Use Tentar novamente.";
                return null;
            }
        }

        if (!themeEnabled && !tapPresent)
        {
            pending = null;
            State = TrayState.Inactive;
            LastError = null;
            return null;
        }

        var action = !themeEnabled ? OperationAction.Reset :
            tapPresent ? OperationAction.Reload : OperationAction.Load;
        pending = new OperationRequest(checked(++nextId), explorer.Value, action);
        pendingThemeEnabled = themeEnabled;
        attemptedInGeneration = true;
        LastError = null;
        State = TrayState.Waiting;
        return pending;
    }

    public OperationRequest BeginExport(ExplorerIdentity explorer, bool themeEnabled)
    {
        if (explorer.ProcessId <= 0 || explorer.CreationTimeUtcFileTime <= 0)
            throw new ArgumentException("Explorer identity is invalid.", nameof(explorer));

        // Export can load a previously unseen Explorer. Its failure must belong
        // to that generation, so the next safety poll cannot call it a new shell.
        CurrentExplorer = explorer;
        pending = new OperationRequest(checked(++nextId), explorer, OperationAction.Export);
        pendingThemeEnabled = themeEnabled;
        attemptedInGeneration = true;
        LastError = null;
        State = TrayState.Waiting;
        return pending;
    }

    public bool Complete(long requestId, bool success, string? error = null)
    {
        if (pending is not { } request || request.Id != requestId)
            return false;
        pending = null;
        State = success ? pendingThemeEnabled ? TrayState.Active : TrayState.Inactive : TrayState.Failed;
        LastError = success ? null : string.IsNullOrWhiteSpace(error) ? "A operação falhou." : error;
        return true;
    }

    public void Fail(string error)
    {
        pending = null;
        State = TrayState.Failed;
        LastError = string.IsNullOrWhiteSpace(error) ? "A operação falhou." : error;
    }
}
