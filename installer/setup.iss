; OpenRGB Audio Refresh & Priority Plugin - Inno Setup 6 Installer Script
; Author: Ronanj Molina (RubyJ / @rjmolina13)
; Repository: https://github.com/rjmolina13/OpenRGBAudioRefreshPlugin
;
; Installs OpenRGBAudioRefreshPlugin.dll into %APPDATA%\OpenRGB\plugins without requiring
; UAC elevation, keeping uninstaller metadata isolated in %LOCALAPPDATA%\OpenRGBAudioRefreshPlugin
; so OpenRGB's PluginManager::ScanAndLoadPluginsFrom never scans non-plugin files.

#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif

#ifndef SourceDir
  #define SourceDir "..\build"
#endif

[Setup]
AppId={{8C3D2F6A-9E41-4A12-B7F9-5108E24B19C3}
AppName=OpenRGB Audio Refresh & Priority Plugin
AppVersion={#MyAppVersion}
AppVerName=OpenRGB Audio Refresh & Priority Plugin v{#MyAppVersion}
AppPublisher=Ronanj Molina (RubyJ / @rjmolina13)
AppPublisherURL=https://github.com/rjmolina13/OpenRGBAudioRefreshPlugin
AppSupportURL=https://github.com/rjmolina13/OpenRGBAudioRefreshPlugin/issues
AppUpdatesURL=https://github.com/rjmolina13/OpenRGBAudioRefreshPlugin/releases
DefaultDirName={userappdata}\OpenRGB\plugins
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallFilesDir={localappdata}\OpenRGBAudioRefreshPlugin
SetupIconFile=..\icons\OpenRGBAudioRefreshPlugin.ico
UninstallDisplayIcon={localappdata}\OpenRGBAudioRefreshPlugin\OpenRGBAudioRefreshPlugin.ico
LicenseFile=..\LICENSE.txt
OutputBaseFilename=OpenRGBAudioRefreshPlugin-v{#MyAppVersion}-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#SourceDir}\OpenRGBAudioRefreshPlugin.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\icons\OpenRGBAudioRefreshPlugin.ico"; DestDir: "{localappdata}\OpenRGBAudioRefreshPlugin"; Flags: ignoreversion

[UninstallDelete]
Type: files; Name: "{userappdata}\OpenRGB\plugins\OpenRGBAudioRefreshPlugin.dll"
Type: dirifempty; Name: "{localappdata}\OpenRGBAudioRefreshPlugin"

[Code]
var
  WasHotReplacedWhileRunning: Boolean;

function GetTickCount: DWORD;
  external 'GetTickCount@kernel32.dll stdcall';

procedure StageOutExistingLockedDll();
var
  PluginsDir: String;
  TargetDll: String;
  TempDir: String;
  OldTempPath: String;
  FindRec: TFindRec;
  CandidatePath: String;
begin
  WasHotReplacedWhileRunning := False;
  PluginsDir := ExpandConstant('{app}');
  if not DirExists(PluginsDir) then
    ForceDirectories(PluginsDir);

  TempDir := GetEnv('TEMP');
  if TempDir = '' then
    TempDir := ExpandConstant('{localappdata}\Temp');

  { Clean up or stage out any existing OpenRGBAudioRefreshPlugin*.dll files (including legacy versioned names) }
  if FindFirst(PluginsDir + '\OpenRGBAudioRefreshPlugin*.dll', FindRec) then
  begin
    try
      repeat
        if FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY = 0 then
        begin
          CandidatePath := PluginsDir + '\' + FindRec.Name;
          if not DeleteFile(CandidatePath) then
          begin
            { DLL is currently loaded by OpenRGB.exe - move out of plugins\ to %TEMP% on the same volume }
            OldTempPath := TempDir + '\OpenRGBAudioRefreshPlugin_' + IntToStr(GetTickCount) + '_' + FindRec.Name + '.old';
            if RenameFile(CandidatePath, OldTempPath) then
              WasHotReplacedWhileRunning := True;
          end;
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;

  TargetDll := PluginsDir + '\OpenRGBAudioRefreshPlugin.dll';
  if FileExists(TargetDll) then
  begin
    if not DeleteFile(TargetDll) then
    begin
      OldTempPath := TempDir + '\OpenRGBAudioRefreshPlugin_' + IntToStr(GetTickCount) + '.dll.old';
      if RenameFile(TargetDll, OldTempPath) then
        WasHotReplacedWhileRunning := True;
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    StageOutExistingLockedDll();
  end;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result :=
    'Plugin Target Directory:' + NewLine +
    Space + ExpandConstant('{app}') + NewLine + NewLine +
    'Uninstaller Metadata Directory (isolated from OpenRGB plugin scanner):' + NewLine +
    Space + ExpandConstant('{localappdata}\OpenRGBAudioRefreshPlugin') + NewLine + NewLine +
    'Target Compatibility:' + NewLine +
    Space + 'OpenRGB 0.9+ / 1.0rc3 (64-bit Windows, Plugin API v4, Qt 5.15.0 MSVC x64)';
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpFinished) and WasHotReplacedWhileRunning then
  begin
    WizardForm.FinishedLabel.Caption :=
      WizardForm.FinishedLabel.Caption + #13#10#13#10 +
      'Note: OpenRGB is currently running. Please restart OpenRGB to load the newly updated OpenRGBAudioRefreshPlugin.dll.';
  end;
end;
