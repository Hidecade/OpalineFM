#ifndef AppVersion
  #error AppVersion must be supplied by build-windows-installers.ps1
#endif
#ifndef BuildRoot
  #error BuildRoot must be supplied by build-windows-installers.ps1
#endif
#ifndef OutputDirectory
  #error OutputDirectory must be supplied by build-windows-installers.ps1
#endif

[Setup]
AppId={{BC3FE809-E82B-48D5-8E1B-73BEE43AB906}
AppName=Opaline FM
AppVersion={#AppVersion}
AppPublisher=Hidecade
DefaultDirName={autopf}\Opaline FM
DefaultGroupName=Opaline FM
DisableProgramGroupPage=yes
OutputDir={#OutputDirectory}
OutputBaseFilename=OpalineFM-{#AppVersion}-Windows-x64-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\Opaline FM.exe
VersionInfoVersion={#AppVersion}
VersionInfoCompany=Hidecade
VersionInfoDescription=Opaline FM installer
VersionInfoProductName=Opaline FM
VersionInfoProductVersion={#AppVersion}
CloseApplications=yes
RestartApplications=no

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "standalone"; Description: "Standalone application"; Types: full custom; Flags: fixed
Name: "vst3"; Description: "VST3 instrument plug-in"; Types: full custom

[Files]
Source: "{#BuildRoot}\Standalone\Opaline FM.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#BuildRoot}\VST3\Opaline FM.vst3\*"; DestDir: "{commoncf64}\VST3\Opaline FM.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Opaline FM"; Filename: "{app}\Opaline FM.exe"; WorkingDir: "{app}"; Components: standalone
Name: "{autodesktop}\Opaline FM"; Filename: "{app}\Opaline FM.exe"; WorkingDir: "{app}"; Components: standalone; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Run]
Filename: "{app}\Opaline FM.exe"; Description: "Launch Opaline FM"; Flags: nowait postinstall skipifsilent; Components: standalone

