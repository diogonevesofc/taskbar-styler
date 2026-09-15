; SPDX-License-Identifier: GPL-3.0-or-later
; Compile through tools/build-installer.ps1. No downloads or application launch.
#ifndef PackageDirectory
  #error PackageDirectory must point to the audited self-contained package.
#endif
#ifndef ReleaseVersion
  #define ReleaseVersion "0.1.0-beta.1"
#endif
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef InstallerOutput
  #error InstallerOutput must be specified.
#endif

[Setup]
AppId={{A5C94A48-037D-487B-85BD-8147123672BB}
AppName=Taskbar Styler
AppVersion={#AppVersion}
AppVerName=Taskbar Styler {#ReleaseVersion}
AppPublisher=diogonevesofc
AppPublisherURL=https://github.com/diogonevesofc/taskbar-styler
AppSupportURL=https://github.com/diogonevesofc/taskbar-styler/issues
DefaultDirName={autopf}\Taskbar Styler
DefaultGroupName=Taskbar Styler
DisableProgramGroupPage=yes
DisableWelcomePage=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.22000
AppMutex=Local\TaskbarStyler.Tray
CloseApplications=no
RestartApplications=no
UninstallFilesDir={app}\uninstall
UninstallDisplayIcon={app}\versions\{#ReleaseVersion}\TaskbarStyler.Tray.exe
LicenseFile={#PackageDirectory}\LICENSE
OutputDir={#InstallerOutput}
#ifdef InstallerTest
OutputBaseFilename=TaskbarStyler-{#ReleaseVersion}-win-x64-installer-test
#else
OutputBaseFilename=TaskbarStyler-{#ReleaseVersion}-win-x64-setup
#endif
VersionInfoVersion={#AppVersion}.0
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
brazilianportuguese.SetupAppRunningError=%1 está em execução.%n%nNo aplicativo, abra Diagnóstico > Ferramentas > Desativar e sair. Fechar a janela apenas a esconde.%n%nDepois clique em OK para continuar, ou Cancelar para sair.
brazilianportuguese.UninstallAppRunningError=%1 está em execução.%n%nNo aplicativo, abra Diagnóstico > Ferramentas > Desativar e sair. Fechar a janela apenas a esconde.%n%nDepois clique em OK para continuar, ou Cancelar para sair.
english.SetupAppRunningError=%1 is running.%n%nIn the application, open Diagnóstico > Ferramentas > Desativar e sair. Closing the window only hides it.%n%nThen click OK to continue, or Cancel to exit.
english.UninstallAppRunningError=%1 is running.%n%nIn the application, open Diagnóstico > Ferramentas > Desativar e sair. Closing the window only hides it.%n%nThen click OK to continue, or Cancel to exit.

[Files]
; A new release never overwrites an Explorer-loaded DLL from the old release.
; Repairing the same release can defer replacement until a voluntary reboot.
Source: "{#PackageDirectory}\*"; DestDir: "{app}\versions\{#ReleaseVersion}"; Flags: ignoreversion recursesubdirs createallsubdirs restartreplace uninsrestartdelete

[Icons]
Name: "{group}\Taskbar Styler"; Filename: "{app}\versions\{#ReleaseVersion}\TaskbarStyler.Tray.exe"; WorkingDir: "{app}\versions\{#ReleaseVersion}"
Name: "{group}\Desinstalar Taskbar Styler"; Filename: "{uninstallexe}"; Languages: brazilianportuguese
Name: "{group}\Uninstall Taskbar Styler"; Filename: "{uninstallexe}"; Languages: english

[Registry]
; This Windows-owned prerequisite is shared with other diagnostics tools.
; Never overwrite an existing value or remove it on uninstall.
Root: HKLM64; Subkey: "Software\Microsoft\XAML\Debug"; ValueType: dword; ValueName: "DisableCompositionDiag"; ValueData: "1"; Flags: createvalueifdoesntexist

[CustomMessages]
brazilianportuguese.BetaTitle=Versão beta
brazilianportuguese.BetaDescription=Windows 11 x64, com prévia antes de aplicar.
brazilianportuguese.BetaNotice=Esta versão beta inclui o aplicativo e o .NET Desktop Runtime. Não requer download durante a instalação ou o uso. A prévia é ilustrativa; o resultado depende do Windows, do papel de parede e dos ícones. Antes de atualizar ou remover, use “Desativar e sair” no menu de ferramentas do aplicativo. Fechar a janela apenas a esconde. Após atualizar, reinicie o Explorer pelo menu Ferramentas do aplicativo, ou reinicie o Windows, antes de aplicar temas. O instalador não inicia o aplicativo, não configura início automático e não encerra o Explorer. Se a DLL estiver em uso, sua substituição ou remoção poderá aguardar o próximo reinício do Windows. Configurações e logs pessoais serão preservados.
brazilianportuguese.PrerequisiteTitle=Pré-requisito do Windows
brazilianportuguese.PrerequisiteDescription=Autorize a configuração necessária ao XAML Diagnostics.
brazilianportuguese.PrerequisiteNotice=O aplicativo precisa de HKLM\Software\Microsoft\XAML\Debug\DisableCompositionDiag (DWORD) = 1. Se o valor ainda não existir, esta instalação irá criá-lo com sua autorização. Um valor existente será preservado. A desinstalação não apaga essa configuração compartilhada com outras ferramentas do Windows.
brazilianportuguese.PrerequisiteConsent=Autorizo criar esse valor de registro se estiver ausente.
brazilianportuguese.PrerequisiteRequired=Autorize o pré-requisito para continuar. Em instalação silenciosa, use /ACCEPTPREREQUISITE=1.
brazilianportuguese.PrerequisiteConflict=DisableCompositionDiag já existe e não é DWORD 1. O valor foi preservado. Ajuste esse pré-requisito com autorização administrativa antes de instalar.
brazilianportuguese.UnsupportedWindows=Taskbar Styler requer Windows 11 x64 nativo. Windows Server e Windows em ARM não são compatíveis.
brazilianportuguese.ReadyNotice=O aplicativo poderá ser aberto pelo menu Iniciar após a instalação. Configurações e logs pessoais, assim como o pré-requisito compartilhado, não serão removidos ao desinstalar.
english.BetaTitle=Beta release
english.BetaDescription=Windows 11 x64, with a preview before applying.
english.BetaNotice=This beta includes the application and the .NET Desktop Runtime. Installation and use require no downloads. The preview is illustrative; results depend on Windows, wallpaper and icons. Before updating or uninstalling, choose “Disable and exit” (Desativar e sair) in the application's tools menu. Closing the window only hides it. After upgrading, restart Explorer through the application tools menu, or restart Windows, before applying themes. Setup does not launch the application, enable startup or stop Explorer. If the DLL is in use, replacing or deleting it may wait until the next Windows restart. Personal settings and logs are preserved.
english.PrerequisiteTitle=Windows prerequisite
english.PrerequisiteDescription=Authorize the setting required by XAML Diagnostics.
english.PrerequisiteNotice=The application requires HKLM\Software\Microsoft\XAML\Debug\DisableCompositionDiag (DWORD) = 1. If this value is absent, Setup will create it with your consent. An existing value is preserved. Uninstall does not remove this setting shared with other Windows tools.
english.PrerequisiteConsent=I authorize creating this registry value if it is absent.
english.PrerequisiteRequired=Authorize the prerequisite to continue. For silent installation, use /ACCEPTPREREQUISITE=1.
english.PrerequisiteConflict=DisableCompositionDiag already exists and is not DWORD 1. Its value has been preserved. Configure this prerequisite with administrative authorization before installing.
english.UnsupportedWindows=Taskbar Styler requires native x64 Windows 11. Windows Server and Windows on ARM are unsupported.
english.ReadyNotice=Open the application from the Start menu after installation. Personal settings and logs, and the shared prerequisite, are preserved on uninstall.

[Code]
var
  PrerequisitePage: TInputOptionWizardPage;

function PrerequisiteReady: Boolean;
var
  Value: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM64, 'Software\Microsoft\XAML\Debug',
    'DisableCompositionDiag', Value) and (Value = 1);
end;

function InitializeSetup: Boolean;
var
  WindowsVersion: TWindowsVersion;
begin
  GetWindowsVersionEx(WindowsVersion);
  Result := True;
#ifndef InstallerTest
  // The CI-only build omits this check to exercise file/registry installation
  // on Windows Server. It is never uploaded as a distributable artifact.
  Result := WindowsVersion.ProductType = VER_NT_WORKSTATION;
  if not Result then
    SuppressibleMsgBox(CustomMessage('UnsupportedWindows'), mbCriticalError, MB_OK, IDOK);
#endif
end;

procedure InitializeWizard;
var
  BetaPage: TOutputMsgWizardPage;
begin
  BetaPage := CreateOutputMsgPage(wpLicense, CustomMessage('BetaTitle'),
    CustomMessage('BetaDescription'), CustomMessage('BetaNotice'));
  PrerequisitePage := CreateInputOptionPage(BetaPage.ID,
    CustomMessage('PrerequisiteTitle'), CustomMessage('PrerequisiteDescription'),
    CustomMessage('PrerequisiteNotice'), False, False);
  PrerequisitePage.Add(CustomMessage('PrerequisiteConsent'));
  PrerequisitePage.Values[0] := ExpandConstant('{param:ACCEPTPREREQUISITE|0}') = '1';
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = PrerequisitePage.ID) and PrerequisiteReady;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if PrerequisiteReady then Exit;
  if RegValueExists(HKLM64, 'Software\Microsoft\XAML\Debug', 'DisableCompositionDiag') then
    Result := CustomMessage('PrerequisiteConflict')
  else if not PrerequisitePage.Values[0] then
    Result := CustomMessage('PrerequisiteRequired');
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo,
  MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := MemoDirInfo + NewLine + NewLine + CustomMessage('ReadyNotice');
end;
