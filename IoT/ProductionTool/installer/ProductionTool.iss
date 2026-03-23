; -----------------------------------------------------------------------------
; @file ProductionTool.iss
; @description ProductionTool 用 Inno Setup スクリプトです。
;
; 主な仕様:
; - `target\release\production_tool.exe` を `ProductionTool.exe` として配布する
; - `C:\Program Files\IoT\ProductionTool\` へインストールする
; - `C:\ProgramData\IoT\ProductionTool\` に監査ログ・作業領域・鍵素材領域を作成する
; - `LocalServer` の既存導入痕跡がある場合は混在インストールを禁止する
; - アンインストール時は PowerShell 補助スクリプトで安全消去を実行する
;
; [厳守] 配布前提は `インストーラEXE化設計仕様書.md` 3.4 を正とする。
; [禁止] LocalServer と同一端末へ混在インストールしない。
; -----------------------------------------------------------------------------

#define appId "{{B9F417A6-48E0-4D63-9D57-9A8763744C20}"
#define appName "ProductionTool"
#define appVersion "0.1.0"
#define appPublisher "IoT"
#define appExeName "ProductionTool.exe"
#define cargoExeSource "..\target\release\production_tool.exe"
#define defaultInstallDir "C:\Program Files\IoT\ProductionTool"
#define installerOutputDir "build"
#define installerOutputBaseName "ProductionToolSetup"

[Setup]
AppId={#appId}
AppName={#appName}
AppVersion={#appVersion}
AppPublisher={#appPublisher}
DefaultDirName={#defaultInstallDir}
DefaultGroupName=IoT\ProductionTool
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
OutputDir={#installerOutputDir}
OutputBaseFilename={#installerOutputBaseName}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#appExeName}

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[Dirs]
Name: "{commonappdata}\IoT\ProductionTool"
Name: "{commonappdata}\IoT\ProductionTool\logs"
Name: "{commonappdata}\IoT\ProductionTool\logs\audit"
Name: "{commonappdata}\IoT\ProductionTool\work"
Name: "{commonappdata}\IoT\ProductionTool\keys"
Name: "{commonappdata}\IoT\InstallerAudit"
Name: "{commonappdata}\IoT\InstallerAudit\ProductionTool"

[Files]
Source: "{#cargoExeSource}"; DestDir: "{app}"; DestName: "{#appExeName}"; Flags: ignoreversion
Source: "..\config\productionTool.settings.example.json"; DestDir: "{app}\config"; Flags: ignoreversion
Source: "..\config\productionTool.settings.installed.example.json"; DestDir: "{app}\config"; DestName: "productionTool.settings.json"; Flags: onlyifdoesntexist
Source: "..\README.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\scripts\install-production-tool.ps1"; DestDir: "{app}\scripts"; Flags: ignoreversion
Source: "..\scripts\uninstall-production-tool.ps1"; DestDir: "{app}\scripts"; Flags: ignoreversion
Source: "..\scripts\build-production-tool-installer.ps1"; DestDir: "{app}\scripts"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\IoT\ProductionTool\ProductionTool"; Filename: "{app}\{#appExeName}"
Name: "{autoprograms}\IoT\ProductionTool\アンインストール"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\{#appExeName}"; Description: "ProductionTool を起動する"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\scripts\uninstall-production-tool.ps1"" -InstallRoot ""{app}"" -AuditRoot ""{commonappdata}\IoT\InstallerAudit\ProductionTool"" -NoConfirm"; Flags: runhidden waituntilterminated

[Code]
function localServerInstallDirExists(): Boolean;
begin
  Result := DirExists(ExpandConstant('C:\Program Files\IoT\LocalServer'));
end;

function localServerTaskExists(): Boolean;
var
  resultCode: Integer;
begin
  Result :=
    Exec(
      ExpandConstant('{cmd}'),
      '/C schtasks /Query /TN "IoT_LocalServer_AutoStart" >NUL 2>&1',
      '',
      SW_HIDE,
      ewWaitUntilTerminated,
      resultCode
    ) and (resultCode = 0);
end;

function localServerSettingsMarkerExists(): Boolean;
begin
  Result :=
    FileExists(ExpandConstant('C:\Program Files\IoT\LocalServer\data\settings.json')) or
    DirExists(ExpandConstant('{commonappdata}\IoT\LocalServer'));
end;

function initializeSetup(): Boolean;
var
  blockedMessage: String;
begin
  if localServerInstallDirExists() or localServerTaskExists() or localServerSettingsMarkerExists() then
  begin
    blockedMessage :=
      'LocalServer の既存導入痕跡を検知したため、ProductionTool のインストールを中断しました。'#13#10#13#10 +
      '確認対象:'#13#10 +
      '- C:\Program Files\IoT\LocalServer'#13#10 +
      '- Scheduled Task: IoT_LocalServer_AutoStart'#13#10 +
      '- LocalServer 設定マーカー / ProgramData 痕跡'#13#10#13#10 +
      'ProductionTool は専用の工場端末へインストールしてください。';
    MsgBox(blockedMessage, mbCriticalError, MB_OK);
    Result := False;
    exit;
  end;

  Result := True;
end;
