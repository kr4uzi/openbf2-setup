; The Windows installer, openbf2-windows-setup.exe (Inno Setup 6). It installs the application folder as the
; build stages it (OpenBF2.exe, Qt, the Visual C++ runtime, setup\, licences\, directx\, the client packages x64\
; and x86\) for the current user, without administrator rights, and then starts OpenBF2, which installs the game
; from the user's disc. The all-users mode follows later (kr4uzi/openbf2-setup issue #1).
;
;   ISCC /DAppVersion=0.3.0-rc2 /DVersionInfo=0.3.0 /DSourceDir=<build>\app /DOutputDir=<dir> /DOutputBaseFilename=openbf2-windows-setup OpenBF2.iss
;
; AppVersion is the OpenBF2 release (Settings > Apps shows it); VersionInfo, its numeric part for the file version,
; defaults to AppVersion.
;
; With /DSign and the sign tool "openbf2" (ISCC "/Sopenbf2=$q<repo>\windows\sign.cmd$q $f"), the installer and the
; uninstaller it writes are signed; the application folder is signed before (windows\build-release.ps1 -Sign).

#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef VersionInfo
  #define VersionInfo AppVersion
#endif
#ifndef SourceDir
  #error SourceDir (the staged application folder) is required
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef OutputBaseFilename
  #define OutputBaseFilename "openbf2-windows-setup"
#endif
#define AppGuid "F434C059-E484-4CE9-B602-35902AC33E1B"
; the uninstall entry Inno writes for the current user
#define UninstallKey "Software\Microsoft\Windows\CurrentVersion\Uninstall\{" + AppGuid + "}_is1"

[Setup]
AppId={{{#AppGuid}}
AppName=OpenBF2
AppVersion={#AppVersion}
AppVerName=OpenBF2 {#AppVersion}
AppPublisher=OpenBF2
AppPublisherURL=https://github.com/kr4uzi/openbf2-setup
VersionInfoVersion={#VersionInfo}
VersionInfoProductTextVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\OpenBF2
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
SetupIconFile=OpenBF2.ico
UninstallDisplayIcon={app}\OpenBF2.exe
UninstallDisplayName=OpenBF2
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
CloseApplications=yes
; Windows' RedirectionGuard (on by default since Inno Setup 6.7) is inherited by OpenBF2 when the last page starts it,
; as for every setup that runs without administrative rights. OpenBF2 would then refuse to follow the junctions in
; its game data that it created itself, the links to the user's mods and into an installation used in place. This
; per-user installation writes only where the user can write anyway, so the guard protects nothing here.
RedirectionGuard=no
#ifdef Sign
SignTool=openbf2
SignedUninstaller=yes
#endif

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\OpenBF2"; Filename: "{app}\OpenBF2.exe"
Name: "{userdesktop}\OpenBF2"; Filename: "{app}\OpenBF2.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\OpenBF2.exe"; Description: "Start OpenBF2"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; the 64-bit d3dx9_25.dll which OpenBF2 took from the game disc (src/directx.h)
Type: files; Name: "{app}\x64\d3dx9_25.dll"
Type: dirifempty; Name: "{app}\x64"
Type: dirifempty; Name: "{app}"

[Code]
// The version an earlier run installed for this user (Inno's uninstall entry), or '' on a first installation.
function InstalledVersion(): String;
begin
  if not RegQueryStringValue(HKCU, '{#UninstallKey}', 'DisplayVersion', Result) then
    Result := '';
end;

// A second run updates or repairs the installed copy. It keeps the shortcut choice of the first run (UsePreviousTasks)
// instead of asking again; the Ready page says what Install does and where OpenBF2 is removed.
function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = wpSelectTasks) and (InstalledVersion() <> '');
end;

// Takes the decimal number at the start of S off S; -1 if S does not start with one (of at most five digits).
function TakeNumber(var S: String): Integer;
var
  N: Integer;
begin
  N := 0;
  while (N < Length(S)) and (S[N + 1] >= '0') and (S[N + 1] <= '9') do
    N := N + 1;
  if (N = 0) or (N > 5) then
    Result := -1
  else begin
    Result := StrToInt(Copy(S, 1, N));
    Delete(S, 1, N);
  end;
end;

// Takes Prefix off the start of S if S starts with it.
function TakePrefix(var S: String; const Prefix: String): Boolean;
begin
  Result := Copy(S, 1, Length(Prefix)) = Prefix;
  if Result then
    Delete(S, 1, Length(Prefix));
end;

// A key that sorts OpenBF2 releases (the engine's tags without the v) in the order they are made:
// 0.3.0-rc1 < 0.3.0-pre-rc2 < 0.3.0-pre-rc2.1 < 0.3.0-rc2 < 0.3.0 < 0.3.1-rc1. '' for a version of another form,
// which cannot be ordered.
function ReleaseKey(Version: String): String;
var
  Major, Minor, Patch, Candidate, Stage, Build: Integer;
begin
  Result := '';
  Major := TakeNumber(Version);
  if (Major < 0) or not TakePrefix(Version, '.') then Exit;
  Minor := TakeNumber(Version);
  if (Minor < 0) or not TakePrefix(Version, '.') then Exit;
  Patch := TakeNumber(Version);
  if Patch < 0 then Exit;
  // the release itself ranks after all of its candidates
  Candidate := 99999;
  Stage := 1;
  Build := 0;
  if Version <> '' then begin
    if not TakePrefix(Version, '-') then Exit;
    // pre-rcN (and its builds pre-rcN.M) come before rcN
    if TakePrefix(Version, 'pre-') then Stage := 0;
    if not TakePrefix(Version, 'rc') then Exit;
    Candidate := TakeNumber(Version);
    if Candidate < 0 then Exit;
    if TakePrefix(Version, '.') then begin
      Build := TakeNumber(Version);
      if Build < 0 then Exit;
    end;
    if Version <> '' then Exit;
  end;
  Result := Format('%.5d.%.5d.%.5d.%.5d.%d.%.5d', [Major, Minor, Patch, Candidate, Stage, Build]);
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
var
  Installed, OldKey, NewKey: String;
begin
  Installed := InstalledVersion();
  Result := '';
  if Installed <> '' then begin
    OldKey := ReleaseKey(Installed);
    NewKey := ReleaseKey('{#AppVersion}');
    if Installed = '{#AppVersion}' then
      Result := 'OpenBF2 ' + Installed + ' is installed. Install reinstalls it.'
    else if (OldKey <> '') and (NewKey <> '') and (NewKey > OldKey) then
      Result := 'OpenBF2 ' + Installed + ' is installed. Install updates it to {#AppVersion}.'
    else if (OldKey <> '') and (NewKey <> '') and (NewKey < OldKey) then
      Result := 'OpenBF2 ' + Installed + ' is installed. Install replaces it with the older {#AppVersion}.'
    else
      Result := 'OpenBF2 ' + Installed + ' is installed. Install replaces it with {#AppVersion}.';
    Result := Result + NewLine + NewLine;
  end;
  Result := Result + MemoDirInfo;
  if MemoTasksInfo <> '' then
    Result := Result + NewLine + NewLine + MemoTasksInfo;
  if Installed <> '' then
    Result := Result + NewLine + NewLine + 'To remove OpenBF2, use Settings ' + #$2192 + ' Apps.';
end;

// The game data holds directory junctions: to the user's own mods, and for an installation used in place into
// that installation's folder (src/existing.h). They are removed first and never entered, so that DelTree below
// cannot reach into a folder outside OpenBF2's own data.
procedure RemoveLinks(const Dir: String);
var
  Find: TFindRec;
  Path: String;
begin
  if FindFirst(AddBackslash(Dir) + '*', Find) then
    try
      repeat
        Path := AddBackslash(Dir) + Find.Name;
        if (Find.Name <> '.') and (Find.Name <> '..') then begin
          // $400: FILE_ATTRIBUTE_REPARSE_POINT, a junction or a symbolic link
          if (Find.Attributes and $400) <> 0 then begin
            if (Find.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then RemoveDir(Path) else DeleteFile(Path);
          end else if (Find.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
            RemoveLinks(Path);
        end;
      until not FindNext(Find);
    finally
      FindClose(Find);
    end;
end;

// The installed game, its settings and the CD keys stay unless the user asks: they are in %LOCALAPPDATA%\OpenBF2,
// apart from the application. Documents\Battlefield 2 (profiles) is never touched.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Data: String;
begin
  if (CurUninstallStep = usPostUninstall) and not UninstallSilent then begin
    Data := ExpandConstant('{localappdata}\OpenBF2');
    if DirExists(Data) and (MsgBox('Also remove the installed game, its settings and the CD keys in' + #13#10 + Data + '?' + #13#10#13#10 +
        'Your profiles in Documents\Battlefield 2 stay.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES) then begin
      RemoveLinks(Data);
      DelTree(Data, True, True, True);
    end;
  end;
end;
