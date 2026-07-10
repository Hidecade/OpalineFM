# Opaline FM

Opaline FM is a free 4-operator FM synthesizer built with C++ and JUCE.

It is inspired by classic 1980s digital FM instruments and supports compatible 32-voice SysEx banks. The project provides a shared FM engine with standalone, VST3, plugin standalone, and macOS AU build paths.

Opaline FM is an independent project and is not affiliated with Yamaha.

![Opaline FM user interface](docs/images/opalinefm-ui.png)

## Features

- 4-operator FM engine
- OLD and NEW/chip-hybrid render models
- compatible 32-voice SysEx bank loading and saving
- Pitch Envelope Generator with measured timing/level behavior
- LFO pitch and amplitude modulation
- MIDI note input, pitch bend, and mod wheel support
- Reused standalone-style editor in the plugin build
- VST3 instrument build for Windows-compatible DAWs
- AU build path for Logic Pro on macOS

## Build Requirements

- CMake 3.22 or newer
- C++17 compiler
- JUCE checkout at `external/JUCE` or a path supplied with `OPALINE_JUCE_DIR`
- Visual Studio Build Tools on Windows
- Xcode on macOS for AU builds

## Windows Build

Configure and build the VST3:

```powershell
cmake --preset standalone-vs-debug
cmake --build --preset plugin-vs-debug
```

Build the plugin standalone app:

```powershell
cmake --build --preset plugin-standalone-vs-debug
```

Build the full standalone app:

```powershell
cmake --build --preset standalone-vs-debug
```

Expected debug outputs:

```text
build/standalone-vs-debug/OpalineFM_Plugin_artefacts/Debug/VST3/Opaline FM.vst3
build/standalone-vs-debug/OpalineFM_Plugin_artefacts/Debug/Standalone/Opaline FM.exe
build/standalone-vs-debug/OpalineFM_Standalone_artefacts/Debug/Opaline FM.exe
```

## macOS AU Build

```bash
cmake --preset macos-debug
cmake --build --preset plugin-au-macos-debug
```

Install the generated component into:

```text
~/Library/Audio/Plug-Ins/Components/
```

## VST3 Installation on Windows

Copy the whole `.vst3` bundle folder:

```text
Opaline FM.vst3
```

to:

```text
C:\Program Files\Common Files\VST3\
```

Then rescan plugins in the DAW.

## Windows Installers

Install Inno Setup 6 or 7, then build the release binaries and both Windows installers:

```powershell
.\scripts\build-windows-installers.ps1 -Version 0.3.0
```

The generated standalone and VST3 installers are written to `dist/`.

## Voice Files

Opaline FM can load compatible 32-voice bulk SysEx banks:

```text
.syx
```

It can also export its own voice library XML:

```text
.opalinelibrary.xml
```

Plugin standalone full-state files use:

```text
.opalinefmstate
```

Factory voice banks are not required for the engine to run. Public releases should avoid bundling third-party or copyrighted factory banks unless redistribution rights are confirmed.

## Documentation

Repository summary:

```text
docs/OpalineFM_Spec.md
```

Detailed private/research notes can live outside the repository. Keep this repository focused on public-facing build, release, and compatibility documentation.

## Legal Notes

Opaline FM is unofficial and not affiliated with Yamaha Corporation.

Yamaha and compatible, where mentioned, are used only to describe compatibility with historical file formats and synthesis behavior.

JUCE is dual-licensed under AGPLv3/commercial terms. Binary distribution must comply with the selected JUCE license. VST3 SDK and other third-party notices should be included with public releases.

## Current Status

The project is in an early public-name transition stage. The engine and plugin builds work, but release packaging, licensing notices, factory-bank policy, and RT-safety cleanup still need review before a polished public release.
