# SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidatePattern('^\d+\.\d+\.\d+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?$')]
    [string]$Version = '0.1.0-beta.1',
    [string]$CompilerPath,
    # CI-only build: permits Windows Server for install/uninstall tests.
    # Its distinct filename must never be included in release artifacts.
    [switch]$InstallerTest
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$PackageDirectory = (Resolve-Path -LiteralPath $PackageDirectory).ProviderPath.TrimEnd('\')
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
if ($OutputDirectory.Equals($PackageDirectory, [StringComparison]::OrdinalIgnoreCase) -or
    $OutputDirectory.StartsWith($PackageDirectory + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'A saída do instalador deve ficar fora do pacote de entrada.'
}
if (($PackageDirectory + $OutputDirectory).IndexOfAny([char[]]'"' + [char[]]"`r`n") -ge 0) {
    throw 'Os caminhos não podem conter aspas ou quebras de linha.'
}
if ($InstallerTest -and $env:GITHUB_ACTIONS -ne 'true') {
    throw 'A variante de teste do instalador só pode ser compilada no GitHub Actions.'
}

foreach ($name in @('TaskbarStyler.Tray.exe', 'TaskbarStyler.Tray.dll', 'TaskbarStyler.Tray.Core.dll',
    'TaskbarStyler.Tray.runtimeconfig.json', 'TaskbarStyler.Tray.deps.json', 'TaskbarStyler.Tap.dll',
    'taskbar-styler.exe', 'coreclr.dll', 'hostfxr.dll', 'hostpolicy.dll', 'System.Windows.Forms.dll',
    'LICENSE', 'NOTICE', 'THEMES.md', 'THIRD_PARTY_NOTICES.md',
    'licenses/nlohmann-json-LICENSE.MIT', 'licenses/doctest-LICENSE.txt', 'licenses/cppwinrt-LICENSE.txt',
    'licenses/dotnet-winforms-10.0.12-THIRD-PARTY-NOTICES.TXT', 'licenses/dotnet-wpf-10.0.12-THIRD-PARTY-NOTICES.TXT',
    'licenses/microsoft.netcore.app.runtime.win-x64-10.0.12-LICENSE.TXT',
    'licenses/microsoft.netcore.app.runtime.win-x64-10.0.12-THIRD-PARTY-NOTICES.TXT',
    'licenses/microsoft.windowsdesktop.app.runtime.win-x64-10.0.12-LICENSE')) {
    if (-not (Test-Path -LiteralPath (Join-Path $PackageDirectory $name) -PathType Leaf)) {
        throw "Pacote offline incompleto: $name. Publique com tools/build-tray.ps1 -SelfContained."
    }
}
$runtime = Get-Content -LiteralPath (Join-Path $PackageDirectory 'TaskbarStyler.Tray.runtimeconfig.json') -Raw | ConvertFrom-Json
if ($runtime.runtimeOptions.PSObject.Properties.Name -contains 'framework' -or
    $runtime.runtimeOptions.PSObject.Properties.Name -contains 'frameworks') {
    throw 'O pacote depende de um runtime global; publique com -SelfContained.'
}
$included = @($runtime.runtimeOptions.includedFrameworks | ForEach-Object { $_.name })
if ('Microsoft.NETCore.App' -notin $included -or 'Microsoft.WindowsDesktop.App' -notin $included) {
    throw 'O pacote não declara os runtimes .NET Core e Windows Desktop incluídos.'
}

# Only native binaries have an x64 PE machine requirement; managed assemblies
# legitimately use an AnyCPU PE header and are checked by dotnet's own build.
foreach ($name in @('TaskbarStyler.Tray.exe', 'TaskbarStyler.Tap.dll', 'taskbar-styler.exe')) {
    $reader = [IO.BinaryReader]::new([IO.File]::OpenRead((Join-Path $PackageDirectory $name)))
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "PE inválido: $name" }
        $reader.BaseStream.Position = 0x3c
        $offset = $reader.ReadInt32()
        if ($offset -lt 0 -or $offset -gt $reader.BaseStream.Length - 6) { throw "PE truncado: $name" }
        $reader.BaseStream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) {
            throw "O binário nativo não é x64: $name"
        }
    } finally { $reader.Dispose() }
}
$entries = @(Get-ChildItem -LiteralPath $PackageDirectory -Recurse -Force)
if (@($entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
    throw 'O pacote não pode conter links ou pontos de nova análise.'
}
if (@($entries | Where-Object { -not $_.PSIsContainer -and $_.Name -in @('config.json', 'log.txt', 'visual-tree.txt') }).Count) {
    throw 'O pacote contém arquivos pessoais de configuração ou diagnóstico.'
}
$sourceThemes = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'themes') -File -Filter '*.json')
$packageThemes = @(Get-ChildItem -LiteralPath (Join-Path $PackageDirectory 'themes') -File -Filter '*.json')
if (@($sourceThemes | Where-Object Name -ne 'credits.json').Count -ne 55 -or
    $sourceThemes.Count -ne $packageThemes.Count) { throw 'O catálogo deve conter os 55 temas e os créditos atuais.' }
foreach ($theme in $sourceThemes) {
    $copy = Join-Path $PackageDirectory ('themes\' + $theme.Name)
    if (-not (Test-Path -LiteralPath $copy -PathType Leaf) -or
        (Get-FileHash -LiteralPath $copy).Hash -ne (Get-FileHash -LiteralPath $theme.FullName).Hash) {
        throw "Tema ausente ou diferente do repositório: $($theme.Name)"
    }
}

if (-not $CompilerPath) {
    $compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($compiler) { $CompilerPath = $compiler.Source }
    else {
        foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
            if ($base) {
                $candidate = Join-Path $base 'Inno Setup 6\ISCC.exe'
                if (Test-Path -LiteralPath $candidate -PathType Leaf) { $CompilerPath = $candidate; break }
            }
        }
    }
}
if (-not $CompilerPath -or -not (Test-Path -LiteralPath $CompilerPath -PathType Leaf)) {
    throw 'Compilador Inno Setup 6 não encontrado. Use -CompilerPath; este script não instala ferramentas.'
}
$CompilerPath = (Resolve-Path -LiteralPath $CompilerPath).ProviderPath
function Get-InnoCompilerVersion([string]$Banner) {
    # ISCC's PE version resource is 0.0.0.0. The loaded compiler engine emits
    # its actual version during compilation (ISCC.dpr, ShowBanner/Go).
    $match = [regex]::Match($Banner,
        '(?m)^Compiler engine version: Inno Setup (?<version>\d+\.\d+\.\d+(?:\.\d+)?)[ \t]*\r?$')
    if (-not $match.Success) { throw 'ISCC não informou uma versão reconhecida do motor de compilação.' }
    $compilerVersion = [version]$match.Groups['version'].Value
    if ($compilerVersion.Major -ne 6 -or $compilerVersion.Minor -lt 3) {
        throw "É necessário Inno Setup 6.3 ou superior da série 6; encontrado $compilerVersion."
    }
    return $compilerVersion
}
$appVersion = ($Version -split '-', 2)[0]
foreach ($component in $appVersion.Split('.')) {
    if ([uint64]$component -gt 65535) { throw 'Os componentes numéricos da versão não podem exceder 65535.' }
}
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$suffix = if ($InstallerTest) { 'installer-test' } else { 'setup' }
$artifact = Join-Path $OutputDirectory "TaskbarStyler-$Version-win-x64-$suffix.exe"
if (Test-Path -LiteralPath $artifact) { throw "A saída já existe. Use um diretório novo: $artifact" }
$compilerArguments = @("/DPackageDirectory=$PackageDirectory", "/DReleaseVersion=$Version",
    "/DAppVersion=$appVersion", "/DInstallerOutput=$OutputDirectory")
if ($InstallerTest) { $compilerArguments += '/DInstallerTest=1' }
& $CompilerPath @compilerArguments (Join-Path $repoRoot 'installer\taskbar-styler.iss') |
    Tee-Object -Variable compilerOutput
if ($LASTEXITCODE -ne 0) { throw "Inno Setup falhou com código $LASTEXITCODE." }
$compilerVersion = Get-InnoCompilerVersion ($compilerOutput -join [Environment]::NewLine)
Write-Host "Compilador validado: Inno Setup $compilerVersion ($CompilerPath)"
if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) { throw 'Inno Setup não produziu o instalador esperado.' }
Write-Output $artifact
