#define MyAppName "LastFrame"
#ifndef MyAppVersion
  #define MyAppVersion "0.1.6"
#endif
#ifndef SourceDir
  #define SourceDir "LastFrame-windows-x64-portable"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{D5A1B8C7-8F1B-4E50-9D1D-6C0E5B7F7A16}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=LastFrame
AppPublisherURL=https://github.com/glebKovshov/LastFrame
AppSupportURL=https://github.com/glebKovshov/LastFrame/issues
DefaultDirName={localappdata}\Programs\LastFrame
DefaultGroupName=LastFrame
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir={#OutputDir}
OutputBaseFilename=LastFrame-windows-x64-installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\logo\lastframe-icon.ico
UninstallDisplayIcon={app}\LastFrame.exe
LicenseFile=..\LICENSE

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\LastFrame"; Filename: "{app}\LastFrame.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\LastFrame"; Filename: "{app}\LastFrame.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\LastFrame.exe"; Description: "Launch LastFrame"; Flags: nowait postinstall skipifsilent
