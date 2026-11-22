; The Windows installer, openbf2-windows-setup.exe (Inno Setup 6). It installs the application folder as the
; build stages it (OpenBF2.exe, Qt, the Visual C++ runtime, setup\, licences\, directx\, the client packages x64\
; and x86\) for the current user, without administrator rights, and then starts OpenBF2, which installs the game
; from the user's disc. The all-users mode follows later (kr4uzi/openbf2-setup issue #1).
;
;   ISCC /DAppVersion=0.5.0 /DSourceDir=<build>\app /DOutputDir=<dir> /DOutputBaseFilename=openbf2-windows-setup OpenBF2.iss
;
; With /DSign and the sign tool "openbf2" (ISCC "/Sopenbf2=$q<repo>\windows\sign.cmd$q $f"), the installer and the
; uninstaller it writes are signed; the application folder is signed before (windows\build-release.ps1 -Sign).

#ifndef AppVersion
  #error AppVersion is required
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

[Setup]
AppId={{F434C059-E484-4CE9-B602-35902AC33E1B}
AppName=OpenBF2
AppVersion={#AppVersion}
AppVerName=OpenBF2 {#AppVersion}
AppPublisher=OpenBF2
AppPublisherURL=https://github.com/kr4uzi/openbf2-setup
VersionInfoVersion={#AppVersion}
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
// The installed game, its settings and the CD keys stay unless the user asks: they are in %LOCALAPPDATA%\OpenBF2,
// apart from the application. Documents\Battlefield 2 (profiles) is never touched.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Data: String;
begin
  if (CurUninstallStep = usPostUninstall) and not UninstallSilent then begin
    Data := ExpandConstant('{localappdata}\OpenBF2');
    if DirExists(Data) and (MsgBox('Also remove the installed game, its settings and the CD keys in' + #13#10 + Data + '?' + #13#10#13#10 +
        'Your profiles in Documents\Battlefield 2 stay.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES) then
      DelTree(Data, True, True, True);
  end;
end;
