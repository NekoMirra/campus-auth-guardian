; Campus Auth Guardian 安装包脚本
; 由 CI 动态填充版本号与产物路径

#define MyAppName "校园网认证守护"
#define MyAppNameEn "Campus Auth Guardian"
#ifndef MyAppVersion
#define MyAppVersion "2.2.0"
#endif
#define MyAppExeName "CampusAuthGuardian.exe"

[Setup]
AppId={{8A3F2C71-5B9E-4D0A-9C4E-2F1B7A6D8E03}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} v{#MyAppVersion}
DefaultDirName={autopf}\{#MyAppNameEn}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=.
OutputBaseFilename=CampusAuthGuardian-setup-x64-{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupIconFile=..\csharp\app.ico

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Files]
; 全部 SelfContained 产物（exe/dll/pri/资源目录）
Source: "..\publish-x64\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "开机自动启动校园网认证守护"; GroupDescription: "其他选项:"; Flags: unchecked

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[Registry]
; 开机自启（HKCU，无需管理员）
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "CampusAuthGuardian"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: autostart

[UninstallRun]
; 卸载时清理自启注册表
Filename: "reg.exe"; Parameters: "DELETE HKCU\Software\Microsoft\Windows\CurrentVersion\Run /V CampusAuthGuardian /F"; Flags: runhidden

[UninstallDelete]
; 保留用户配置（config.ini 不删）；清理日志
Type: files; Name: "{app}\campus_auth.log"
Type: files; Name: "{app}\campus_auth.log.1"
