#ifndef PayloadDir
  #error PayloadDir must point to the prepared installer payload.
#endif
#ifndef OutputDir
  #error OutputDir must point to the installer output directory.
#endif
#ifndef AppVersion
  #error AppVersion must match the bundled engine version.
#endif
#ifndef AppFileVersion
  #error AppFileVersion must contain the four-part Windows file version.
#endif
#ifndef RepoRoot
  #error RepoRoot must point to the Funke repository.
#endif

[Setup]
AppId={{36454A75-515B-49FC-82E8-2DC876A3FEA5}
AppName=Funke
AppVersion={#AppVersion}
AppVerName=Funke {#AppVersion}
AppPublisher=Funke
AppPublisherURL=https://github.com/Ben-Scr/Index
AppSupportURL=https://github.com/Ben-Scr/Index/issues
VersionInfoVersion={#AppFileVersion}
VersionInfoDescription=Funke Setup
DefaultDirName={localappdata}\Programs\Funke
DefaultGroupName=Funke
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible and not arm64
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
DisableProgramGroupPage=yes
DisableWelcomePage=no
DisableDirPage=no
UsePreviousAppDir=yes
UsePreviousSetupType=yes
UsePreviousTasks=yes
WizardStyle=modern
WizardResizable=yes
WizardSizePercent=120,135
WizardImageFile={#RepoRoot}\Funke-Runtime\FunkeAssets\Textures\icon_128.png
WizardSmallImageFile=
WizardImageStretch=no
SetupIconFile={#RepoRoot}\Funke-Launcher\Icon.ico
UninstallDisplayIcon={app}\Funke-Launcher.exe
LicenseFile={#RepoRoot}\LICENSE.txt
OutputDir={#OutputDir}
OutputBaseFilename=FunkeSetup-{#AppVersion}-Windows-x64
Compression=lzma2/normal
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
Uninstallable=yes
UninstallDisplayName=Funke
SetupLogging=yes
#ifdef SignToolName
SignTool={#SignToolName}
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"

[CustomMessages]
english.Recommended=Recommended
german.Recommended=Empfohlen
english.Full=All components
german.Full=Alle Komponenten
english.Custom=Choose components
german.Custom=Komponenten auswählen
english.Core=Funke Launcher and Editor (required)
german.Core=Funke Launcher und Editor (erforderlich)
english.Templates=Project templates (2D and 3D)
german.Templates=Projektvorlagen (2D und 3D)
english.CLI=Command-line tools
german.CLI=Werkzeuge für die Kommandozeile
english.OptionalPackages=Additional packages
german.OptionalPackages=Zusätzliche Pakete
english.DesktopShortcut=Create a desktop shortcut
german.DesktopShortcut=Desktop-Verknüpfung erstellen
english.Launch=Start Funke
german.Launch=Funke starten
english.ComponentHint=Additional packages are made available to the Editor. Enable them in each project's Package Manager. Your projects are stored separately and are kept when Funke is uninstalled.
german.ComponentHint=Zusätzliche Pakete stehen im Editor bereit. Aktiviere sie im Paketmanager des jeweiligen Projekts. Deine Projekte liegen separat und bleiben beim Deinstallieren von Funke erhalten.
english.Prerequisites=Required components:
german.Prerequisites=Benötigte Komponenten:
english.Installed=already installed
german.Installed=bereits installiert
english.WillInstall=will be installed
german.WillInstall=wird installiert
english.PrerequisiteNotice=Missing Microsoft components are included in this setup. Windows may ask for administrator permission to install them. They remain available to other applications after Funke is uninstalled.
german.PrerequisiteNotice=Fehlende Microsoft-Komponenten sind im Setup enthalten. Windows kann für deren Installation Administratorrechte anfordern. Sie bleiben nach dem Deinstallieren von Funke für andere Programme verfügbar.
english.InstallingPrerequisite=Installing %1...
german.InstallingPrerequisite=%1 wird installiert...
english.PrerequisiteFailed=%1 could not be installed (error %2). Complete its installation and try again. Funke has not been installed.
german.PrerequisiteFailed=%1 konnte nicht installiert werden (Fehler %2). Schließe dessen Installation ab und versuche es erneut. Funke wurde noch nicht installiert.
english.PrerequisiteCancelled=Installation of %1 was cancelled. Allow Windows to install this required component, then try again. Funke has not been installed.
german.PrerequisiteCancelled=Die Installation von %1 wurde abgebrochen. Erlaube Windows, diese benötigte Komponente zu installieren, und versuche es erneut. Funke wurde noch nicht installiert.
english.PrerequisiteUnavailable=%1 is still unavailable. Repair the Microsoft component and run Funke Setup again. Funke has not been installed.
german.PrerequisiteUnavailable=%1 ist noch nicht verfügbar. Repariere die Microsoft-Komponente und starte das Funke-Setup erneut. Funke wurde noch nicht installiert.
english.RestartPrerequisites=Windows needs to restart to finish installing the required components. Restart your computer, then run Funke Setup again.
german.RestartPrerequisites=Windows muss die Installation der benötigten Komponenten durch einen Neustart abschließen. Starte den Computer neu und führe danach das Funke-Setup erneut aus.

[Types]
Name: "recommended"; Description: "{cm:Recommended}"
Name: "full"; Description: "{cm:Full}"
Name: "custom"; Description: "{cm:Custom}"; Flags: iscustom

[Components]
Name: "core"; Description: "{cm:Core}"; Types: recommended full custom; Flags: fixed
Name: "templates"; Description: "{cm:Templates}"; Types: recommended full
Name: "cli"; Description: "{cm:CLI}"; Types: full
#include PayloadDir + "\Components.iss"

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopShortcut}"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\Prerequisites\vc_redist.x64.exe"; Flags: dontcopy
Source: "{#PayloadDir}\Prerequisites\dotnet-sdk.exe"; Flags: dontcopy
Source: "{#PayloadDir}\Core\*"; DestDir: "{app}"; Components: core; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PayloadDir}\Templates\*"; DestDir: "{app}"; Components: templates; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PayloadDir}\CLI\*"; DestDir: "{app}"; Components: cli; Flags: ignoreversion recursesubdirs createallsubdirs
#include PayloadDir + "\Files.iss"

[InstallDelete]
#include PayloadDir + "\PackageDelete.iss"
#include PayloadDir + "\SdkDelete.iss"

[Icons]
Name: "{group}\Funke"; Filename: "{app}\Funke-Launcher.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Funke"; Filename: "{app}\Funke-Launcher.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\Funke-Launcher.exe"; WorkingDir: "{app}"; Description: "{cm:Launch}"; Flags: nowait postinstall skipifsilent; Check: CanLaunchFunke

[Code]
var
  PrerequisiteRestartRequired: Boolean;
  DotNetDirectory: String;

function SetEnvironmentVariable(Name, Value: String): Boolean;
  external 'SetEnvironmentVariableW@kernel32.dll stdcall';

function HasStableDotNet10SDKAt(const Directory: String): Boolean;
var
  ExitCode, I, Separator: Integer;
  Output: TExecOutput;
  Version: String;
begin
  Result := False;
  if not FileExists(AddBackslash(Directory) + 'dotnet.exe') then Exit;
  try
    if not ExecAndCaptureOutput(AddBackslash(Directory) + 'dotnet.exe',
      '--list-sdks', ExpandConstant('{tmp}'), SW_HIDE,
      ewWaitUntilTerminated, ExitCode, Output) then Exit;
    if (ExitCode <> 0) or Output.Error then Exit;
    for I := 0 to GetArrayLength(Output.StdOut) - 1 do begin
      Separator := Pos(' ', Trim(Output.StdOut[I]));
      if Separator > 0 then begin
        Version := Copy(Trim(Output.StdOut[I]), 1, Separator - 1);
        if (Copy(Version, 1, 5) = '10.0.') and (Pos('-', Version) = 0) then begin
          DotNetDirectory := Directory;
          Result := True;
          Exit;
        end;
      end;
    end;
  except
    Log('Could not query the .NET SDK: ' + GetExceptionMessage);
  end;
end;

function HasDotNet10SDK: Boolean;
var
  RegisteredDirectory: String;
begin
  Result := HasStableDotNet10SDKAt(ExpandConstant('{commonpf64}\dotnet'));
  if Result then Exit;
  if RegQueryStringValue(HKLM64,
    'SOFTWARE\dotnet\Setup\InstalledVersions\x64', 'InstallLocation', RegisteredDirectory) then
    Result := HasStableDotNet10SDKAt(RegisteredDirectory);
  if Result then Exit;
  if RegQueryStringValue(HKLM32,
    'SOFTWARE\dotnet\Setup\InstalledVersions\x64', 'InstallLocation', RegisteredDirectory) then
    Result := HasStableDotNet10SDKAt(RegisteredDirectory);
end;

function HasVisualCppRuntime: Boolean;
var
  Installed, Major, Minor: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM64,
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed)
    and RegQueryDWordValue(HKLM64,
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Major', Major)
    and RegQueryDWordValue(HKLM64,
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Minor', Minor);
  Result := Result and (Installed = 1) and
    ((Major > 14) or ((Major = 14) and (Minor >= 44)));
end;

function InstallPrerequisite(const FileName, DisplayName: String): String;
var
  ExitCode: Integer;
begin
  Result := '';
  WizardForm.PreparingLabel.Caption := FmtMessage(CustomMessage('InstallingPrerequisite'), [DisplayName]);
  ExtractTemporaryFile(FileName);
  if not ShellExec('runas', ExpandConstant('{tmp}\') + FileName,
    '/install /passive /norestart', '', SW_SHOWNORMAL, ewWaitUntilTerminated, ExitCode) then begin
    if ExitCode = 1223 then
      Result := FmtMessage(CustomMessage('PrerequisiteCancelled'), [DisplayName])
    else
      Result := FmtMessage(CustomMessage('PrerequisiteFailed'), [DisplayName, IntToStr(ExitCode)]);
    Exit;
  end;
  Log(Format('%s installer exit code: %d', [DisplayName, ExitCode]));
  if (ExitCode = 3010) or (ExitCode = 1641) then
    PrerequisiteRestartRequired := True
  else if ExitCode = 1602 then
    Result := FmtMessage(CustomMessage('PrerequisiteCancelled'), [DisplayName])
  else if ExitCode <> 0 then
    Result := FmtMessage(CustomMessage('PrerequisiteFailed'), [DisplayName, IntToStr(ExitCode)]);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not HasVisualCppRuntime then begin
    Result := InstallPrerequisite('vc_redist.x64.exe', 'Microsoft Visual C++ Runtime');
    if Result <> '' then Exit;
  end;
  if not HasDotNet10SDK then begin
    Result := InstallPrerequisite('dotnet-sdk.exe', 'Microsoft .NET 10 SDK');
    if Result <> '' then Exit;
  end;
  if not HasVisualCppRuntime then
    Result := FmtMessage(CustomMessage('PrerequisiteUnavailable'), ['Microsoft Visual C++ Runtime'])
  else if not HasDotNet10SDK then
    Result := FmtMessage(CustomMessage('PrerequisiteUnavailable'), ['Microsoft .NET 10 SDK']);
  if Result <> '' then begin
    if PrerequisiteRestartRequired then begin
      NeedsRestart := True;
      Result := CustomMessage('RestartPrerequisites');
    end;
    Exit;
  end;
  SetEnvironmentVariable('PATH', DotNetDirectory + ';' + GetEnv('PATH'));
  SetEnvironmentVariable('DOTNET_ROOT_X64', DotNetDirectory);
end;

function NeedRestart: Boolean;
begin
  Result := PrerequisiteRestartRequired;
end;

function CanLaunchFunke: Boolean;
begin
  Result := not PrerequisiteRestartRequired;
end;

function PrerequisiteStatus(const DisplayName: String; Installed: Boolean): String;
begin
  if Installed then
    Result := DisplayName + ' - ' + CustomMessage('Installed')
  else
    Result := DisplayName + ' - ' + CustomMessage('WillInstall');
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := MemoDirInfo + NewLine + NewLine + MemoComponentsInfo;
  if MemoTasksInfo <> '' then Result := Result + NewLine + NewLine + MemoTasksInfo;
  Result := Result + NewLine + NewLine + CustomMessage('Prerequisites') + NewLine +
    Space + PrerequisiteStatus('Microsoft .NET 10 SDK', HasDotNet10SDK) + NewLine +
    Space + PrerequisiteStatus('Microsoft Visual C++ Runtime', HasVisualCppRuntime) +
    NewLine + NewLine + CustomMessage('PrerequisiteNotice');
end;

procedure InitializeWizard;
var
  ComponentHint: TNewStaticText;
begin
  WizardForm.ReadyMemo.WordWrap := True;
  WizardForm.ReadyMemo.ScrollBars := ssVertical;
  ComponentHint := TNewStaticText.Create(WizardForm);
  ComponentHint.Parent := WizardForm.SelectComponentsPage;
  ComponentHint.AutoSize := False;
  ComponentHint.WordWrap := True;
  ComponentHint.Left := WizardForm.ComponentsList.Left;
  ComponentHint.Width := WizardForm.ComponentsList.Width;
  ComponentHint.Height := ScaleY(52);
  ComponentHint.Top := WizardForm.ComponentsList.Top + WizardForm.ComponentsList.Height - ComponentHint.Height;
  ComponentHint.Anchors := [akLeft, akRight, akBottom];
  ComponentHint.Caption := CustomMessage('ComponentHint');
  WizardForm.ComponentsList.Height := WizardForm.ComponentsList.Height - ComponentHint.Height - ScaleY(8);
end;
