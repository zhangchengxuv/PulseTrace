; Version comes only from CMake metadata. No user database is a [Files] entry.
#ifndef AppVersion
 #error AppVersion is required
#endif
[Setup]
AppId={{A9F4B181-98CE-426B-AE31-7112540CB549}
AppName=PulseTrace
AppVersion={#AppVersion}
AppVerName=PulseTrace {#AppVersion}
AppPublisher=PulseTrace Project
AppPublisherURL=https://github.com/zhangchengxuv/PulseTrace
DefaultDirName={code:DefaultInstallPath}
DefaultGroupName={param:GROUP|PulseTrace}
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupArchitecture=x64
OutputDir={#OutputDir}
OutputBaseFilename=PulseTrace-Setup-v{#AppVersion}-x64
SetupIconFile={#SourceRoot}\assets\PulseTrace.ico
UninstallDisplayIcon={app}\PulseTrace.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
DisableWelcomePage=no
LanguageDetectionMethod=uilanguage
ShowLanguageDialog=yes
VersionInfoVersion={#BaseVersion}.0
VersionInfoProductVersion={#BaseVersion}.0
VersionInfoProductTextVersion={#AppVersion}
[Languages]
Name: english; MessagesFile: compiler:Default.isl
Name: chinesesimp; MessagesFile: compiler:Languages\ChineseSimplified.isl
[CustomMessages]
english.desktop=Create a desktop shortcut
chinesesimp.desktop=创建桌面快捷方式
english.importtask=Import portable history (optional)
chinesesimp.importtask=导入便携版历史数据（可选）
english.start=Start PulseTrace
chinesesimp.start=启动 PulseTrace
english.datapreview=Data directory: %1
chinesesimp.datapreview=数据目录：%1
english.fallback=Drive D is unavailable. The default uses your user application directory. You can choose another location.
chinesesimp.fallback=D 盘不可用，默认改用当前用户的应用目录。你仍可选择其他安装位置。
english.importtitle=Import existing data
chinesesimp.importtitle=导入已有数据
english.importsubtitle=Copy existing data safely. The original directory is preserved.
chinesesimp.importsubtitle=检测到旧数据时可安全复制，原目录会保留。
english.importdesc=Choose an old or portable data folder, or leave empty to keep current data. Existing destination data will not be overwritten or merged.
chinesesimp.importdesc=选择旧版或便携版的 data 文件夹；留空则保留当前数据。目标已有数据时不会覆盖或自动合并。
english.importlabel=Existing data directory (optional):
chinesesimp.importlabel=旧数据目录（可留空）：
english.choicetitle=Two data sets detected
chinesesimp.choicetitle=检测到两份数据
english.choicesub=Choose which data to use
chinesesimp.choicesub=请选择保留方式
english.choicedesc=Both the destination and old directory contain history. Neither data set will be deleted automatically.
chinesesimp.choicedesc=目标目录和旧目录均有历史数据。两份数据都不会自动删除。
english.keeptarget=Keep using the existing destination data
chinesesimp.keeptarget=继续使用目标目录现有数据
english.usesource=Use the old data (requires an empty destination)
chinesesimp.usesource=使用旧数据（需选择空的安装目录）
english.occupied=The destination already contains data. Choose an empty installation directory or keep the destination data. Databases will not be overwritten or merged.
chinesesimp.occupied=目标目录已有数据。请返回选择空的安装目录，或选择继续使用目标数据；不会覆盖或合并数据库。
english.patherror=The directory is not writable or does not support secure permissions. Choose another location, or explicitly run an elevated installation if required.
chinesesimp.patherror=安装目录不可写或不支持安全文件权限。请选择其他目录；若需提升权限，请退出并明确选择管理员安装。
english.identityerror=The original signed-in user could not be identified. Installation stopped to avoid granting data access to the wrong account.
chinesesimp.identityerror=无法确定最初登录用户，已停止安装，以免把数据权限授予错误账户。
english.closeerror=Fully exit PulseTrace from its tray menu, resolve save errors, and retry. The installer will not force-terminate it.
chinesesimp.closeerror=请从系统托盘完全退出 PulseTrace，解决保存错误后重试。安装器不会强制结束进程。
english.backuperror=A consistent backup could not be created. Upgrade has not started; existing data is unchanged.
chinesesimp.backuperror=无法创建一致性备份，升级未开始，原有数据保持不变。
english.importerror=Data copy or validation failed. Original data is preserved. Check destination diagnostics and retry with a new empty directory.
chinesesimp.importerror=数据复制或验证失败。旧数据保留；请查看目标目录诊断并使用新的空目录重试。
english.permissionerror=Could not secure program/data permissions. Choose another installation directory. Parent Program Files permissions are never modified.
chinesesimp.permissionerror=无法安全设置程序或数据目录权限，请选择其他安装目录。不会修改上级 Program Files 的权限。
english.runtime=Microsoft Edge WebView2 Runtime is required. Install the Evergreen Runtime from Microsoft and reopen the app. It is not installed automatically.
chinesesimp.runtime=需要 Microsoft Edge WebView2 Runtime。请从微软官网下载 Evergreen Runtime 后重新打开应用。安装器不会自动安装运行时。
english.uninstallerror=Fully exit PulseTrace from its tray menu and retry. Historical data was not deleted.
chinesesimp.uninstallerror=请从托盘完全退出 PulseTrace 后重试。历史数据未删除。
english.readydata=Data is stored at: %1. Uninstall keeps history by default.
chinesesimp.readydata=数据保存在：%1。卸载默认保留历史。
[Tasks]
Name: desktopicon; Description: "{cm:desktop}"; Flags: unchecked
Name: portableimport; Description: "{cm:importtask}"; Flags: unchecked
[Files]
Source: "{#StageDir}\PulseTrace.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\PulseTraceWidget.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\PulseTraceMaintenance.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\WebView2Loader.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\web\desktop\*"; DestDir: "{app}\web\desktop"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs
Source: "{#StageDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\THIRD-PARTY-NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\PulseTrace"; Filename: "{app}\PulseTrace.exe"
Name: "{userdesktop}\PulseTrace"; Filename: "{app}\PulseTrace.exe"; Tasks: desktopicon
[Run]
Filename: "{app}\PulseTrace.exe"; Description: "{cm:start}"; Flags: nowait postinstall skipifsilent runasoriginaluser
[UninstallDelete]
Type: files; Name: "{app}\installed.ini"
[Code]
var ImportPage: TInputQueryWizardPage;
    ChoicePage: TInputOptionWizardPage;
    DataPreview: TNewStaticText;
    DataPath, BackupPath, OriginalSID, LegacySource: String;
    MaintenanceReady, PermissionsUnlocked: Boolean;

function Quote(const S: String): String;
begin Result := '"' + S + '"'; end;

function HasOption(Name: String): Boolean;
var I: Integer;
begin
 Result := False;
 for I := 1 to ParamCount do if CompareText(ParamStr(I), '/' + Name) = 0 then Result := True;
end;
function DefaultInstallPath(Param: String): String;
begin
  if DirExists('D:\') then Result := 'D:\Program Files\PulseTrace'
  else Result := ExpandConstant('{localappdata}\Programs\PulseTrace');
end;

procedure ExtractMaintenance;
begin
  if MaintenanceReady then Exit;
  ExtractTemporaryFile('PulseTraceMaintenance.exe');
  MaintenanceReady := True;
end;

function Maintain(const Args: String): Boolean;
var ExitCode: Integer;
begin
  ExtractMaintenance;
  Result := Exec(ExpandConstant('{tmp}\PulseTraceMaintenance.exe'), Args, '', SW_HIDE, ewWaitUntilTerminated, ExitCode) and (ExitCode = 0);
end;

procedure DirChanged(Sender: TObject);
begin DataPreview.Caption := FmtMessage(CustomMessage('datapreview'), [AddBackslash(WizardDirValue) + 'data']); end;

procedure InitializeWizard;
begin
  DataPreview := TNewStaticText.Create(WizardForm); DataPreview.Parent := WizardForm.SelectDirPage;
  DataPreview.Left := WizardForm.DirEdit.Left; DataPreview.Top := WizardForm.DirEdit.Top + WizardForm.DirEdit.Height + ScaleY(12);
  DataPreview.Width := WizardForm.DirEdit.Width; DataPreview.Height := ScaleY(48); DataPreview.WordWrap := True;
  WizardForm.DirEdit.OnChange := @DirChanged; DirChanged(nil);
  ImportPage := CreateInputQueryPage(wpSelectTasks, CustomMessage('importtitle'), CustomMessage('importsubtitle'), CustomMessage('importdesc'));
  ImportPage.Add(CustomMessage('importlabel'), False);
  ImportPage.Values[0] := ExpandConstant('{param:PORTABLESOURCE|}');
  ChoicePage := CreateInputOptionPage(ImportPage.ID, CustomMessage('choicetitle'), CustomMessage('choicesub'), CustomMessage('choicedesc'), True, False);
  ChoicePage.Add(CustomMessage('keeptarget')); ChoicePage.Add(CustomMessage('usesource')); ChoicePage.SelectedValueIndex := 0;
  if not DirExists('D:\') and not WizardSilent then MsgBox(CustomMessage('fallback'), mbInformation, MB_OK);
end;

procedure CurPageChanged(CurPageID: Integer);
var Existing: String;
begin
  if CurPageID = wpSelectDir then DirChanged(nil);
  if CurPageID = ImportPage.ID then begin
    Existing := GetIniString('PulseTrace', 'DataDirectory', '', ExpandConstant('{app}\installed.ini'));
    if (Existing <> '') and (CompareText(Existing, AddBackslash(WizardDirValue)+'data') <> 0) and FileExists(AddBackslash(Existing)+'monitor.db') then LegacySource := Existing;
    if (LegacySource = '') and not HasOption('NOLEGACYDETECT') and FileExists(ExpandConstant('{localappdata}\PulseTrace\data\monitor.db')) then LegacySource := ExpandConstant('{localappdata}\PulseTrace\data');
    if (ImportPage.Values[0] = '') and (LegacySource <> '') then ImportPage.Values[0] := LegacySource;
  end;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
var Existing: String;
begin
  Result := False;
  Existing := GetIniString('PulseTrace', 'DataDirectory', '', ExpandConstant('{app}\installed.ini'));
  if PageID = ImportPage.ID then Result := (ImportPage.Values[0] = '') and not WizardIsTaskSelected('portableimport') and ((Existing = '') or (CompareText(Existing, AddBackslash(WizardDirValue)+'data') = 0)) and (HasOption('NOLEGACYDETECT') or not FileExists(ExpandConstant('{localappdata}\PulseTrace\data\monitor.db')));
  if PageID = ChoicePage.ID then Result := not FileExists(AddBackslash(WizardDirValue)+'data\monitor.db') or not FileExists(AddBackslash(ImportPage.Values[0])+'monitor.db') or (CompareText(ImportPage.Values[0], AddBackslash(WizardDirValue)+'data') = 0);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then begin
    Result := Maintain('--check-install-path ' + Quote(WizardDirValue));
    if not Result then MsgBox(CustomMessage('patherror'), mbError, MB_OK);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var ExitCode: Integer; Source, SIDFile, Existing, Owner: String; SIDBytes: AnsiString;
begin
  Result := ''; DataPath := ExpandConstant('{app}\data');
  if not Maintain('--check-install-path ' + Quote(ExpandConstant('{app}'))) then begin Result := CustomMessage('patherror'); Exit; end;
  ExtractMaintenance; SIDFile := ExpandConstant('{tmp}\original-user.sid');
  if not ExecAsOriginalUser(ExpandConstant('{tmp}\PulseTraceMaintenance.exe'), '--write-user-sid ' + Quote(SIDFile), '', SW_HIDE, ewWaitUntilTerminated, ExitCode) or (ExitCode <> 0) or not LoadStringFromFile(SIDFile, SIDBytes) then begin Result := CustomMessage('identityerror'); Exit; end;
  OriginalSID := Trim(String(SIDBytes));
  Owner := GetIniString('PulseTrace', 'OwnerSID', '', ExpandConstant('{app}\installed.ini'));
  if (Owner <> '') and (Owner <> OriginalSID) then begin Result := CustomMessage('identityerror'); Exit; end;
  if FileExists(ExpandConstant('{app}\PulseTrace.exe')) then begin
    if not Maintain('--close ' + Quote(ExpandConstant('{app}\PulseTrace.exe'))) then begin Result := CustomMessage('closeerror'); Exit; end;
    if not Maintain('--permissions ' + Quote(ExpandConstant('{app}')) + ' ' + Quote(OriginalSID) + ' unlock') then begin Result := CustomMessage('permissionerror'); Exit; end;
    PermissionsUnlocked := True;
  end;
  Source := ImportPage.Values[0];
  Existing := GetIniString('PulseTrace', 'DataDirectory', '', ExpandConstant('{app}\installed.ini'));
  if (Source = '') and (Existing <> '') and (CompareText(Existing,DataPath) <> 0) then Source := Existing;
  if (Source <> '') and (CompareText(Source,DataPath) <> 0) and FileExists(AddBackslash(Source)+'monitor.db') then begin
    if FileExists(AddBackslash(DataPath)+'monitor.db') then begin
      if ChoicePage.SelectedValueIndex = 0 then Source := ''
      else begin Result := CustomMessage('occupied'); Exit; end;
    end;
    if Source <> '' then begin
      if not Maintain('--quiesce-data ' + Quote(Source)) then begin Result := CustomMessage('closeerror'); Exit; end;
      BackupPath := AddBackslash(Source)+'backups\pre-transfer-'+GetDateTimeString('yyyymmdd-hhnnss-zzz','-',':');
      if not Maintain('--backup ' + Quote(Source) + ' ' + Quote(BackupPath)) then begin Result := CustomMessage('backuperror'); Exit; end;
      if not Maintain('--import ' + Quote(Source) + ' ' + Quote(DataPath)) then begin Result := CustomMessage('importerror'); Exit; end;
    end;
  end;
  if FileExists(AddBackslash(DataPath)+'monitor.db') then begin
    BackupPath := AddBackslash(DataPath)+'backups\upgrade-'+GetDateTimeString('yyyymmdd-hhnnss-zzz','-',':');
    if not Maintain('--backup ' + Quote(DataPath) + ' ' + Quote(BackupPath)) then begin Result := CustomMessage('backuperror'); Exit; end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var ExitCode: Integer; InitialLanguage: String;
begin
 if CurStep = ssPostInstall then begin
  if not ForceDirectories(DataPath) then RaiseException(CustomMessage('patherror'));
  SetIniString('PulseTrace','DataDirectory',DataPath,ExpandConstant('{app}\installed.ini'));
  SetIniString('PulseTrace','OwnerSID',OriginalSID,ExpandConstant('{app}\installed.ini'));
  SetIniString('PulseTrace','LayoutVersion','2',ExpandConstant('{app}\installed.ini'));
  if ActiveLanguage = 'chinesesimp' then InitialLanguage := 'zh-CN' else InitialLanguage := 'en-US';
  if not FileExists(AddBackslash(DataPath)+'config.json') then SaveStringToFile(AddBackslash(DataPath)+'config.json','{"language":"'+InitialLanguage+'"}',False);
  if BackupPath <> '' then SetIniString('PulseTrace','UpgradeBackup',BackupPath,ExpandConstant('{app}\installed.ini'));
  if not Maintain('--permissions ' + Quote(ExpandConstant('{app}')) + ' ' + Quote(OriginalSID) + ' seal') then RaiseException(CustomMessage('permissionerror'));
  if not Maintain('--existing-autostart ' + Quote(ExpandConstant('{app}')) + ' ' + Quote(OriginalSID) + ' migrate') then RaiseException(CustomMessage('identityerror'));
  PermissionsUnlocked := False;
  if not Exec(ExpandConstant('{app}\PulseTraceMaintenance.exe'),'--runtime-check','',SW_HIDE,ewWaitUntilTerminated,ExitCode) or (ExitCode <> 0) then if not WizardSilent then MsgBox(CustomMessage('runtime')+#13#10+'https://developer.microsoft.com/microsoft-edge/webview2/',mbInformation,MB_OK);
 end;
end;

procedure DeinitializeSetup;
begin
 if PermissionsUnlocked then Maintain('--permissions ' + Quote(ExpandConstant('{app}')) + ' ' + Quote(OriginalSID) + ' seal');
end;

function InitializeUninstall: Boolean;
var ExitCode: Integer; Owner: String;
begin
 Result := Exec(ExpandConstant('{app}\PulseTraceMaintenance.exe'),'--close '+Quote(ExpandConstant('{app}\PulseTrace.exe')),'',SW_HIDE,ewWaitUntilTerminated,ExitCode) and (ExitCode=0);
 if not Result then begin MsgBox(CustomMessage('uninstallerror'),mbError,MB_OK); Exit; end;
 Owner := GetIniString('PulseTrace','OwnerSID','',ExpandConstant('{app}\installed.ini'));
 if Owner <> '' then Result := Exec(ExpandConstant('{app}\PulseTraceMaintenance.exe'),'--permissions '+Quote(ExpandConstant('{app}'))+' '+Quote(Owner)+' unlock','',SW_HIDE,ewWaitUntilTerminated,ExitCode) and (ExitCode=0);
 if not Result then begin MsgBox(CustomMessage('permissionerror'),mbError,MB_OK); Exit; end;
 if Owner <> '' then Result := Exec(ExpandConstant('{app}\PulseTraceMaintenance.exe'),'--existing-autostart '+Quote(ExpandConstant('{app}'))+' '+Quote(Owner)+' remove','',SW_HIDE,ewWaitUntilTerminated,ExitCode) and (ExitCode=0);
end;
