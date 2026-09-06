; Campus Auth Guardian ARM64 安装包脚本
; 相对路径以本 .iss 所在目录解析

#define MyAppName "校园网认证守护"
#define MyAppNameEn "Campus Auth Guardian"
#ifndef MyAppVersion
#define MyAppVersion "2.2.4"
#endif
#define MyAppExeName "CampusAuthGuardian.exe"

[Setup]
AppId={{8A3F2C71-5B9E-4D0A-9C4E-2F1B7A6D8E03}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} v{#MyAppVersion} (ARM64)
DefaultDirName={autopf}\{#MyAppNameEn}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputBaseFilename=CampusAuthGuardian-setup-ARM64-{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupIconFile=..\csharp\app.ico

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Files]
Source: "..\publish-arm64\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "开机自动启动校园网认证守护"; GroupDescription: "其他选项:"; Flags: unchecked

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "CampusAuthGuardian"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: autostart

[UninstallRun]
Filename: "reg.exe"; Parameters: "DELETE HKCU\Software\Microsoft\Windows\CurrentVersion\Run /V CampusAuthGuardian /F"; Flags: runhidden

[UninstallDelete]
Type: files; Name: "{app}\campus_auth.log"
Type: files; Name: "{app}\campus_auth.log.1"
