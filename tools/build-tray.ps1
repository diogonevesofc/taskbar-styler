param(
    [string]$NativeDirectory,
    [string]$OutputDirectory,
    [switch]$SelfContained,
    [string]$Version
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $Version) { $Version = (Get-Content -LiteralPath (Join-Path $repoRoot 'VERSION') -Raw).Trim() }
if ($Version -notmatch '^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$') { throw "Versão inválida: $Version" }
if (-not $NativeDirectory) { $NativeDirectory = Join-Path $repoRoot 'build\src\cli' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'out\tray' }
$NativeDirectory = [IO.Path]::GetFullPath($NativeDirectory)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if ($OutputDirectory -eq $NativeDirectory) { throw 'A saída da bandeja deve ser separada da saída nativa.' }
$tap = Join-Path $NativeDirectory 'TaskbarStyler.Tap.dll'
if (-not (Test-Path -LiteralPath $tap -PathType Leaf)) {
    throw "Compile o projeto CMake primeiro. DLL não encontrada: $tap"
}
$sourceThemes = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'themes') -File -Filter '*.json')
$themesOutput = Join-Path $OutputDirectory 'themes'
if (Test-Path -LiteralPath $themesOutput) {
    $extraThemes = @(Get-ChildItem -LiteralPath $themesOutput -File -Filter '*.json' |
        Where-Object { $_.Name -notin $sourceThemes.Name })
    if ($extraThemes.Count -ne 0) {
        throw "A saída contém temas fora do catálogo atual: $($extraThemes.Name -join ', '). Use um diretório de saída novo."
    }
}

# Build-time environment only; the shipped application has no network client.
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
$project = Join-Path $repoRoot 'src\tray\TaskbarStyler.Tray.csproj'
$selfContainedArgument = if ($SelfContained) { 'true' } else { 'false' }
dotnet publish $project --configuration Release --runtime win-x64 --self-contained $selfContainedArgument --output $OutputDirectory --nologo "-p:Version=$Version" -p:DebugType=None -p:DebugSymbols=false
if ($LASTEXITCODE -ne 0) { throw 'Falha ao publicar a bandeja.' }

$tapOutput = Join-Path $OutputDirectory 'TaskbarStyler.Tap.dll'
# A UI-only update can keep the identical DLL already loaded by Explorer.
if (-not (Test-Path -LiteralPath $tapOutput) -or
    (Get-FileHash -LiteralPath $tap).Hash -ne (Get-FileHash -LiteralPath $tapOutput).Hash) {
    Copy-Item -LiteralPath $tap -Destination $tapOutput -Force
}
$cli = Join-Path $NativeDirectory 'taskbar-styler.exe'
if (Test-Path -LiteralPath $cli) { Copy-Item -LiteralPath $cli -Destination $OutputDirectory -Force }
[void][IO.Directory]::CreateDirectory($themesOutput)
$sourceThemes | Copy-Item -Destination $themesOutput -Force
foreach ($file in @('LICENSE','NOTICE','THEMES.md','THIRD_PARTY_NOTICES.md')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot $file) -Destination $OutputDirectory -Force
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'licenses') -Destination $OutputDirectory -Recurse -Force
Set-Content -LiteralPath (Join-Path $OutputDirectory 'VERSION') -Value $Version -Encoding utf8
foreach ($relative in @('TaskbarStyler.Tray.exe','TaskbarStyler.Tray.dll',
    'TaskbarStyler.Tray.runtimeconfig.json','TaskbarStyler.Tap.dll',
    'themes\TranslucentTaskbar.json','LICENSE','NOTICE','THEMES.md')) {
    if (-not (Test-Path -LiteralPath (Join-Path $OutputDirectory $relative) -PathType Leaf)) {
        throw "Pacote incompleto: $relative"
    }
}
if ($SelfContained) {
    foreach ($runtimeFile in @('coreclr.dll','hostfxr.dll','hostpolicy.dll','System.Windows.Forms.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $OutputDirectory $runtimeFile) -PathType Leaf)) {
            throw "Runtime incluído incompleto: $runtimeFile"
        }
    }
    $assets = Get-Content -LiteralPath (Join-Path $repoRoot 'src/tray/obj/project.assets.json') -Raw | ConvertFrom-Json
    $runtimeConfig = Get-Content -LiteralPath (Join-Path $OutputDirectory 'TaskbarStyler.Tray.runtimeconfig.json') -Raw | ConvertFrom-Json
    foreach ($framework in $runtimeConfig.runtimeOptions.includedFrameworks) {
        $packageName = "$($framework.name.ToLowerInvariant()).runtime.win-x64"
        $relativePackage = Join-Path $packageName $framework.version
        $packageRoot = $assets.packageFolders.PSObject.Properties.Name |
            ForEach-Object { Join-Path $_ $relativePackage } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Container } |
            Select-Object -First 1
        if (-not $packageRoot) { throw "Licenças do runtime não encontradas: $relativePackage" }
        $licenseFiles = @(Get-ChildItem -LiteralPath $packageRoot -File |
            Where-Object { $_.Name -match '^(LICENSE(?:\.TXT)?|THIRD-PARTY-NOTICES(?:\.TXT)?)$' })
        if (-not ($licenseFiles | Where-Object { $_.Name -match '^LICENSE' })) { throw "Runtime sem licença: $packageRoot" }
        foreach ($licenseFile in $licenseFiles) {
            Copy-Item -LiteralPath $licenseFile.FullName -Destination (
                Join-Path $OutputDirectory "licenses/$packageName-$($framework.version)-$($licenseFile.Name)") -Force
        }
    }
}
Write-Output "Pacote x64 $Version (runtime incluído: $SelfContained): $OutputDirectory"
