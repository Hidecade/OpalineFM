# Opaline FM

[日本語](README_ja.md) | [Technical specification](docs/OpalineFM_Spec.md)

Opaline FM is a free 4-operator FM synthesizer built with C++ and JUCE. It is inspired by classic 1980s digital FM instruments and can load and save compatible 32-voice SysEx banks.

Opaline FM uses FM synthesis, but it is not a chip emulator and does not attempt to perfectly reproduce any specific FM hardware instrument.

![Opaline FM user interface](docs/images/opalinefm-ui.png)

## Features

- Four-operator FM synthesis with eight algorithms and operator feedback
- FM rendering engine
- Compatible 32-voice SysEx bank import and export
- YM2151/YM2612-related single-voice import from OPM, TFI, VGI, and DMP files
- SINGLE, DUAL, and SPLIT performance modes
- Pitch EG, amplitude EG, LFO, keyboard scaling, velocity, and operator AM
- Pitch bend, modulation wheel, sustain pedal, and portamento control
- Per-voice editing, initialization, copy/paste, load, save, and store operations
- Built-in effects: reverb, delay, chorus, wet mixes, and tone
- WAV recording in the standalone application
- Windows x64 standalone and VST3 installers
- Signed and notarized macOS standalone, VST3, and Audio Unit installer packages

## Downloads

The current public release is **v1.0.9**. Download Windows and macOS installers from the [v1.0.9 release page](https://github.com/Hidecade/OpalineFM/releases/tag/v1.0.9).

Choose the package for the format you need:

- `OpalineFM-Standalone-1.0.9-macOS.pkg`: standalone app. Use this if you want to play Opaline FM without a DAW.
- `OpalineFM-AU-1.0.9-macOS.pkg`: Audio Unit instrument for Logic Pro, GarageBand, and AU hosts.
- `OpalineFM-VST3-1.0.9-macOS.pkg`: VST3 instrument installer for VST3-compatible DAWs.
- `OpalineFM-Standalone-v1.0.9-Windows-x64.exe`: standalone application for 64-bit Windows.
- `OpalineFM-VST3-v1.0.9-Windows-x64.exe`: VST3 instrument installer for 64-bit Windows.

The macOS packages are signed and notarized. The Windows installers are currently unsigned, so Windows may display a publisher warning during installation.

The `Source code` archives shown by GitHub Releases are generated automatically by GitHub. Most users should download the installer for their operating system and desired format.

## Quick Start

1. Launch the standalone application, or insert Opaline FM as an instrument in a DAW.
2. In the standalone application, select an audio output and MIDI input from the header.
3. Select a voice bank, then choose voice A.
4. Play from a MIDI keyboard, the on-screen keyboard, or the PC keyboard in the plugin-standalone application.
5. Adjust the operator and global controls. Use **STORE** to write edits back to the selected library slot.

The VST3 editor intentionally does not turn PC typing keys into notes, so DAW shortcuts remain available. MIDI and the on-screen keyboard continue to work.

## Voice Selection and Libraries

The bank selector chooses one of the loaded 32-voice banks. The A and B selectors choose voices from the current bank; the adjacent arrow buttons step backward or forward through the list.

Top-row library commands:

- **Load**: load a compatible `.syx` bank or an Opaline library XML file.
- **Save**: save the current 32-voice bank as `.syx`.
- **Export**: export the complete multi-bank voice library as XML.

Single-voice commands shown in SINGLE mode:

- **LOAD / SAVE**: load an Opaline or supported chip voice, or write one `.opalinevoice` file.
- **COPY / PASTE**: copy the current voice, including its name, and paste it into the edit buffer.
- **INIT**: initialize the edit buffer.
- **STORE**: write the edited voice and name into the currently selected library slot.

Selecting another voice without using **STORE** discards edits and reloads the stored voice.

## Importing YM2151 and YM2612 Voice Data

In SINGLE mode, use the single-voice **LOAD** button and select one of these files:

| Extension | Source format | Import behavior |
| --- | --- | --- |
| `.opm` | YM2151 VOPM text voice list | Shows the voices in the file and loads the selected voice. |
| `.tfi` | YM2612 TFM Music Maker voice | Loads one 42-byte voice. |
| `.vgi` | YM2612 VGM Music Maker voice | Loads one 43-byte voice, including FMS/AMS. |
| `.dmp` | DefleMask FM preset | Loads supported version 10 or 11 YM2151/YM2612 FM data. |

The importer converts Yamaha operator order, algorithm, feedback, multiplier, detune, total level, envelope rates, sustain level, rate scaling, AM, and available LFO settings into an Opaline voice. YM2151 DT2 is combined with the multiplier and mapped to the nearest available Opaline Ratio value.

Parameters that are not present in the source format are initialized rather than inherited from the previously selected voice. Pitch EG is neutral, effects are disabled, and keyboard level scaling and velocity sensitivity use their defaults. Unsupported SSG-EG behavior is ignored. Some values are necessarily approximated because Opaline FM is not a YM2151/YM2612 chip emulator.

The imported voice is placed in the current edit buffer. Use **STORE** to keep it in the selected library slot, or **SAVE** to write it as an `.opalinevoice` file. On iPhone, an OPM file currently loads its first voice.

## Performance Modes

- **SINGLE** plays voice A only. Single-voice editing and file commands are available in this mode.
- **DUAL** layers voices A and B. **Detune** offsets the B engine and **Balance** sets the A/B level balance.
- **SPLIT** assigns the lower keyboard region to one voice and the upper region to the other. **Split** selects the boundary note.

Each A/B voice can independently use **POLY** or **MONO** operation.

- **POLY** allows multiple simultaneous notes and uses Full Time Portamento.
- **MONO** restricts the engine to one active note and uses Fingered Portamento, which glides only between overlapping key presses.

## Editing a Voice

Opaline FM uses four sine-wave operators. An operator can be heard directly as a **carrier**, or can change another operator's timbre as a **modulator**. The selected algorithm determines these connections.

Global voice controls:

- **ALG** selects one of eight four-operator routing algorithms.
- **FB** controls feedback around operator 4.
- **PITCH EG** uses PR1-PR3 for transition rates and PL1-PL3 for pitch levels.
- **LFO Wave** selects saw up, square, triangle, or sample-and-hold.
- **Sync** restarts the LFO phase at note-on.

Each operator provides:

- **AR, D1R, D1L, D2R, RR**: amplitude-envelope rates and level.
- **Ratio**: oscillator frequency ratio.
- **Detune**: fine frequency offset.
- **Level**: carrier volume or modulator depth.
- **RateSc**: keyboard-dependent envelope-rate scaling.
- **LevelSc**: keyboard-dependent level scaling.
- **Vel**: velocity sensitivity.
- **AM**: enables LFO amplitude modulation for that operator.

Carrier levels mainly change loudness. Modulator levels mainly change harmonic brightness and FM intensity.

## Waveform Display

The display above the voice controls shows the recent left-channel output waveform. While a note is held, the view uses that note as a trigger reference so the waveform stays visually stable instead of drifting across the screen.

The display removes DC offset, chooses a short window around the current note period, and applies automatic gain so quiet and loud patches remain readable. It is intended as a sound-design aid for checking envelope shape, feedback, modulation depth, and the effect of algorithm changes; it is not a calibrated level meter.

## LFO and Modulation

- **Speed** sets the LFO rate.
- **Delay** delays the direct LFO modulation after note-on.
- **PMD / PMS** set direct pitch-modulation depth and sensitivity.
- **AMD / AMS** set direct amplitude-modulation depth and sensitivity.
- **Reverb / Delay / Chorus** set effect amounts.
- **RevMix / DlyMix** set independent wet mixes.
- **Tone** adjusts output tone.

The modulation wheel is a separate modulation source:

- **MOD PITCH** sets its pitch-modulation range and is interpreted through PMS.
- **MOD AMP** sets its amplitude-modulation range and is interpreted through AMS and each operator's AM switch.
- Selecting voice A initializes these two ranges from the voice's PMD and AMD values; they can then be edited independently.

## Wheels, Portamento, and Pedals

- **RANGE** sets pitch-bend range from 0 to 12 semitones. The default is 2.
- **PORTA** sets glide time from 0 to 99. Zero is the shortest time; the mode button controls ON/OFF.
- Each A/B row has an **OFF / FULL / FINGER** button. POLY offers OFF/FULL; MONO offers OFF/FULL/FINGER.
- **FULL** applies portamento from the previous note and can be enabled or disabled with MIDI CC65.
- **FINGER** applies portamento only to overlapping notes and is not gated by CC65.
- MIDI CC64 controls sustain.

Supported standard MIDI controls:

| Control | Function |
| --- | --- |
| Pitch Bend | Pitch wheel, using the RANGE setting |
| CC1 | Modulation wheel |
| CC64 | Sustain pedal |
| CC65 | Portamento foot switch in POLY mode |

## Rendering Engine

Opaline FM uses a single public rendering engine for operator level handling, feedback, attenuation, carrier mixing, and output behavior.

## WAV Recording

In the standalone application, press **WAV** to begin recording. The button changes to **STOP** while recording. After stopping, choose the output filename. Recording is written as stereo WAV.

## Installing Release Builds

### Windows Standalone and VST3

Run the installer for the format you need:

- `OpalineFM-Standalone-v1.0.9-Windows-x64.exe`
- `OpalineFM-VST3-v1.0.9-Windows-x64.exe`

The standalone installer adds the Opaline FM application. The VST3 installer places the plug-in in the standard system VST3 directory:

```text
C:\Program Files\Common Files\VST3\Opaline FM.vst3
```

Restart the DAW or rescan VST3 plug-ins after installation. The Windows installers are currently unsigned, so confirm the publisher warning only when the installer was downloaded from the official Opaline FM GitHub Release.

### macOS Standalone, VST3, and Audio Unit

Run the signed and notarized macOS package for the build you want:

- `OpalineFM-Standalone-1.0.9-macOS.pkg`
- `OpalineFM-VST3-1.0.9-macOS.pkg`
- `OpalineFM-AU-1.0.9-macOS.pkg`

The packages install to the standard macOS application and plug-in locations:

```text
/Applications/
/Library/Audio/Plug-Ins/VST3/
/Library/Audio/Plug-Ins/Components/
```

Restart the DAW or rescan plug-ins after installing VST3 or Audio Unit packages.

In Logic Pro, insert Opaline FM from a software instrument track's **Instrument** slot, not from an Audio FX slot: **AU Instruments > Hidecade > Opaline FM > Stereo**. If it does not appear, open **Logic Pro > Settings > Plug-In Manager** and run **Reset & Rescan Selection** or **Full Audio Unit Reset**, then restart Logic Pro.

## Building from Source

Requirements:

- CMake 3.22 or newer
- A C++17 compiler
- JUCE at `external/JUCE`, or `OPALINE_JUCE_DIR` pointing to another checkout
- Visual Studio Build Tools on Windows
- Xcode on macOS

Windows debug builds:

```powershell
cmake --preset standalone-vs-debug
cmake --build --preset plugin-standalone-vs-debug
cmake --build --preset plugin-vs-debug
```

macOS AU build:

```bash
cmake --preset macos-debug
cmake --build --preset plugin-au-macos-debug
```

Windows development installers require Inno Setup 6 or 7:

```powershell
.\scripts\build-windows-installers.ps1 -Version 1.0.9
```

Generated installers are written to `dist/`.

## File Formats

| Extension | Purpose |
| --- | --- |
| `.syx` | Compatible 32-voice SysEx bank |
| `.opalinevoice` | One Opaline voice |
| `.opm` | YM2151 VOPM text voice list |
| `.tfi` | YM2612 TFM Music Maker single voice |
| `.vgi` | YM2612 VGM Music Maker single voice |
| `.dmp` | Supported DefleMask YM2151/YM2612 FM preset |
| `.opalinelibrary.xml` | Multi-bank Opaline voice library |
| `.opalinefmstate` | Plugin-standalone complete state |

## Documentation

- [Technical specification](docs/OpalineFM_Spec.md)
- [Japanese technical specification](docs/OpalineFM_Spec_ja.md)
- [YM2151/YM2612 voice import specification (Japanese)](docs/YM2151_YM2612_VOICE_IMPORT_SPEC_ja.md)
- [Japanese README](README_ja.md)

## Legal and Project Status

Opaline FM is an independent project that uses FM synthesis. It is not a chip emulator and does not attempt to perfectly reproduce any specific FM hardware instrument. Product names are used only to describe file-format and synthesis compatibility.

Release binaries are currently free to download and use. This repository does not currently grant a broad open-source license for project-specific source code, documentation, images, scripts, or factory patches; do not assume redistribution, reuse, or sublicensing rights unless a license is added or written permission is provided by the rights holder.

Do not redistribute third-party factory banks unless their redistribution rights are confirmed. Binary distribution must comply with the selected JUCE license and include applicable VST3 SDK and third-party notices.

The bundled `assets/factory.syx` bank contains original Opaline FM factory patches created for this project. See [NOTICE.md](NOTICE.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before redistributing source or binaries.

The project remains under active development. Audio-thread locking, FM synthesis behavior, release signing, notarization, and packaging continue to be reviewed.
