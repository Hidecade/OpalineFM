#ifndef AppVersion
  #define AppVersion "0.4.0"
#endif

#ifndef BuildRoot
  #define BuildRoot "..\build\standalone-vs-debug\OpalineFM_Plugin_artefacts\Release"
#endif

[Setup]
AppId={{2E833E47-6A94-441C-BF12-387F73EF6809}
AppName=Opaline FM VST3
AppVersion={#AppVersion}
AppPublisher=Hidecade
DefaultDirName={commoncf64}\VST3\Opaline FM.vst3
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=OpalineFM-VST3-v{#AppVersion}-Windows-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName=Opaline FM VST3 {#AppVersion}
UninstallFilesDir={autopf}\Opaline FM\VST3 Uninstall
CloseApplications=yes
RestartApplications=no

[Files]
Source: "{#BuildRoot}\VST3\Opaline FM.vst3\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
