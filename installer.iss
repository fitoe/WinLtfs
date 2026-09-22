#define MyAppName       "WinLtfs"
#ifndef MyAppVersion
#define MyAppVersion    "0.0.0"
#endif
#define MyAppId         "{61DAB44A-A9EF-4F07-BC82-F0D765A2DB8C}"
#define MyAppPublisher  "rlaphoenix"
#define MyAppURL        "https://github.com/rlaphoenix/WinLtfs"
#define WinFspMsi       "winfsp.msi"

[Setup]
AppId={{#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
VersionInfoVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\ltfs.exe
LicenseFile=LICENSE
OutputDir=Output
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-setup
SetupIconFile=resources\drive-icons\icon1.ico
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
WizardStyle=modern
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.17763
PrivilegesRequired=admin

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "deps_winfsp"; Description: "WinFsp virtual-drive driver  (required - mounting a tape as a drive needs it)"; GroupDescription: "Required dependency:"

[Files]
Source: "dist\*"; DestDir: "{app}"; Excludes: "*.pdb"; \
    Flags: recursesubdirs createallsubdirs ignoreversion
Source: "redist\{#WinFspMsi}"; DestDir: "{tmp}"

[Registry]
Root: HKLM; Subkey: "SOFTWARE\{#MyAppName}"; Flags: uninsdeletekeyifempty
Root: HKLM; Subkey: "SOFTWARE\{#MyAppName}"; ValueType: string; ValueName: "InstallDir"; \
    ValueData: "{app}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "SOFTWARE\{#MyAppName}"; ValueType: string; ValueName: "Version"; \
    ValueData: "{#MyAppVersion}"; Flags: uninsdeletevalue

[Code]
var
  DepPage:     TWizardPage;
  DepLog:      TNewMemo;
  DepProgress: TNewProgressBar;
  DepDone:     Boolean;

function Stamp: String;
begin
  Result := '[' + GetDateTimeString('hh:nn:ss', #0, #0) + ']  ';
end;

procedure DepLogLine(const S: String);
begin
  Log(S);
  if DepLog <> nil then
  begin
    DepLog.Lines.Add(S);
    WizardForm.Refresh;
  end;
end;

procedure SetDepProgress(P: Integer);
begin
  if DepProgress <> nil then
  begin
    DepProgress.Position := P;
    WizardForm.Refresh;
  end;
end;

procedure EnsureDependencyWork;
var
  Code: Integer;
  MsiLog: String;
begin
  if DepDone then Exit;
  DepDone := True;

  DepLogLine(Stamp + 'WinFsp virtual-drive driver');
  if not WizardIsTaskSelected('deps_winfsp') then
    DepLogLine('  skipped - left unchecked')
  else if not FileExists(ExpandConstant('{tmp}\{#WinFspMsi}')) then
    DepLogLine('  ERROR: bundled WinFsp MSI missing - install it manually (https://winfsp.dev)')
  else
  begin
    MsiLog := ExpandConstant('{tmp}\winfsp-install.log');
    DepLogLine('  installing {#WinFspMsi} ...');
    if not Exec('msiexec.exe',
                '/i "' + ExpandConstant('{tmp}\{#WinFspMsi}') + '" /qn /norestart /L*v "' + MsiLog + '"',
                '', SW_HIDE, ewWaitUntilTerminated, Code) then
      DepLogLine('  ERROR: could not start msiexec - mounting needs WinFsp (https://winfsp.dev)')
    else if (Code = 0) or (Code = 1638) or (Code = 3010) then
      DepLogLine('  done (exit ' + IntToStr(Code) + ') - verbose log: ' + MsiLog)
    else
      DepLogLine('  WARNING: msiexec returned ' + IntToStr(Code) + ' - mounting needs WinFsp (https://winfsp.dev)');
  end;

  SetDepProgress(100);
  DepLogLine(Stamp + 'Setup complete.');
end;

procedure InitializeWizard;
begin
  DepPage := CreateCustomPage(wpInstalling, 'Setting up dependencies',
    'WinLtfs is installing the components it needs to run.');

  DepProgress := TNewProgressBar.Create(DepPage);
  DepProgress.Parent := DepPage.Surface;
  DepProgress.Left := 0;
  DepProgress.Top := 0;
  DepProgress.Width := DepPage.SurfaceWidth;
  DepProgress.Height := ScaleY(16);
  DepProgress.Min := 0;
  DepProgress.Max := 100;

  DepLog := TNewMemo.Create(DepPage);
  DepLog.Parent := DepPage.Surface;
  DepLog.Left := 0;
  DepLog.Top := DepProgress.Top + DepProgress.Height + ScaleY(8);
  DepLog.Width := DepPage.SurfaceWidth;
  DepLog.Height := DepPage.SurfaceHeight - DepLog.Top;
  DepLog.ReadOnly := True;
  DepLog.ScrollBars := ssVertical;
  DepLog.WantReturns := False;
  DepLog.Font.Name := 'Consolas';
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (DepPage <> nil) and (CurPageID = DepPage.ID) then
  begin
    WizardForm.BackButton.Enabled := False;
    WizardForm.NextButton.Enabled := False;
    WizardForm.CancelButton.Enabled := False;
    WizardForm.Refresh;
    EnsureDependencyWork;
    WizardForm.NextButton.Enabled := True;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardSilent then
    EnsureDependencyWork;
end;
