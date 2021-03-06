; -- residualvm.iss --
; Inno Setup 5 Script for ResidualVM.

[Setup]
AppCopyright=2012
AppName=ResidualVM
AppVerName=ResidualVM GIT Snapshot
AppPublisher=The ResidualVM Team
AppPublisherURL=http://www.residualvm.org/
AppSupportURL=http://www.residualvm.org/
AppUpdatesURL=http://www.residualvm.org/
DefaultDirName={pf}\ResidualVM
DefaultGroupName=ResidualVM
AllowNoIcons=true
AlwaysUsePersonalGroup=false
EnableDirDoesntExistWarning=false
Compression=lzma
OutputDir=.
OutputBaseFilename=residualvm-win32
DisableStartupPrompt=true
AppendDefaultDirName=false
SolidCompression=true
DirExistsWarning=no
UninstallDisplayIcon={app}\residualvm.exe

[Files]
Source: "AUTHORS.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "COPYING.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "COPYING.LGPL.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "COPYRIGHT.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "NEWS.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "README.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "README-SDL.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "residualvm.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "modern.zip"; DestDir: "{app}"; Flags: ignoreversion
Source: "residualvm-grim-patch.lab"; DestDir: "{app}"; Flags: ignoreversion
Source: "SDL.dll"; DestDir: "{app}"

[Icons]
Name: {group}\{cm:UninstallProgram, ResidualVM}; Filename: {uninstallexe}
Name: {group}\ResidualVM; Filename: {app}\residualvm.exe; WorkingDir: {app}; Comment: residualvm; Flags: createonlyiffileexists
Name: {group}\Authors; Filename: {app}\AUTHORS.txt; WorkingDir: {app}; Comment: AUTHORS; Flags: createonlyiffileexists
Name: {group}\Copying; Filename: {app}\COPYING.txt; WorkingDir: {app}; Comment: COPYING; Flags: createonlyiffileexists
Name: {group}\Copying.LGPL; Filename: {app}\COPYING.LGPL.txt; WorkingDir: {app}; Comment: COPYING.LGPL; Flags: createonlyiffileexists
Name: {group}\Copyright.txt; Filename: {app}\COPYRIGHT.txt; WorkingDir: {app}; Comment: COPYRIGHT; Flags: createonlyiffileexists
Name: {group}\Readme; Filename: {app}\README.txt; WorkingDir: {app}; Comment: README; Flags: createonlyiffileexists
Name: {group}\News; Filename: {app}\NEWS.txt; WorkingDir: {app}; Comment: NEWS; Flags: createonlyiffileexists

[Run]
Filename: {app}\residualvm.exe; Description: Launch ResidualVM; Flags: nowait skipifdoesntexist postinstall skipifsilent
