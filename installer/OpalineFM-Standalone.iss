#ifndef AppVersion
  #define AppVersion "0.3.0.1"
#endif

#ifndef BuildRoot
  #define BuildRoot "..\build\standalone-vs-debug\OpalineFM_Plugin_artefacts\Release"
#endif

[Setup]
AppId={{EFF8E705-A298-415E-BBB3-D3988B714C72}
AppName=Opaline FM Standalone
AppVersion={#AppVersion}
AppPublisher=Hidecade
DefaultDirName={autopf}\Opaline FM
DefaultGroupName=Opaline FM
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=OpalineFM-Standalone-v{#AppVersion}-Windows-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName=Opaline FM Standalone {#AppVersion}
CloseApplications=yes
RestartApplications=no

[Files]
Source: "{#BuildRoot}\Standalone\Opaline FM.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Opaline FM"; Filename: "{app}\Opaline FM.exe"; WorkingDir: "{app}"

[Run]
Filename: "{app}\Opaline FM.exe"; Description: "Launch Opaline FM"; Flags: nowait postinstall skipifsilent
