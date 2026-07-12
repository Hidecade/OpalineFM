# Opaline FM

[日本語](README_ja.md) | [Technical specification](docs/OpalineFM_Spec.md)

Opaline FM is a free 4-operator FM synthesizer built with C++ and JUCE. It is inspired by classic 1980s digital FM instruments and can load and save compatible 32-voice SysEx banks.

Opaline FM is an independent project and is not affiliated with Yamaha Corporation.

![Opaline FM user interface](docs/images/opalinefm-ui.png)

## Features

- Four-operator FM synthesis with eight algorithms and operator feedback
- Type B rendering engine
- Compatible 32-voice SysEx bank import and export
- SINGLE, DUAL, and SPLIT performance modes
- Pitch EG, amplitude EG, LFO, keyboard scaling, velocity, and operator AM
- Pitch bend, modulation wheel, sustain pedal, and portamento control
- Per-voice editing, initialization, copy/paste, load, save, and store operations
- Built-in effects: reverb, delay, chorus, wet mixes, and tone
- WAV recording in the standalone application
- Windows standalone and VST3 builds, plus signed macOS standalone, VST3, and Audio Unit package paths

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

- **LOAD / SAVE**: read or write one `.opalinevoice` file.
- **COPY / PASTE**: copy the current voice, including its name, and paste it into the edit buffer.
- **INIT**: initialize the edit buffer.
- **STORE**: write the edited voice and name into the currently selected library slot.

Selecting another voice without using **STORE** discards edits and reloads the stored voice.

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

Opaline FM uses the Type B rendering engine by default. Type B is the main chip-oriented path for operator level handling, feedback, attenuation, carrier mixing, and output behavior. The legacy Type A comparison control is hidden from the public UI.

## WAV Recording

In the standalone application, press **WAV** to begin recording. The button changes to **STOP** while recording. After stopping, choose the output filename. Recording is written as stereo WAV.

## Installing Release Builds

### Windows Standalone

Run the standalone installer and launch **Opaline FM** from the Start menu. No DAW is required.

### Windows VST3

Run the VST3 installer, or copy the complete `Opaline FM.vst3` bundle to:

```text
C:\Program Files\Common Files\VST3\
```

Rescan plugins in the DAW, create an instrument track, and insert Opaline FM.

### macOS Standalone, VST3, and Audio Unit

Run the signed and notarized macOS package for the build you want:

- `OpalineFM-Standalone-0.3.2.0-macOS.pkg`
- `OpalineFM-VST3-0.3.2.0-macOS.pkg`
- `OpalineFM-AU-0.3.2.0-macOS.pkg`

The packages install to the standard macOS application and plug-in locations:

```text
/Applications/
/Library/Audio/Plug-Ins/VST3/
/Library/Audio/Plug-Ins/Components/
```

Restart the DAW or rescan plug-ins after installing VST3 or Audio Unit packages.

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

Windows installers require Inno Setup 6 or 7:

```powershell
.\scripts\build-windows-installers.ps1 -Version 0.3.2
```

Generated installers are written to `dist/`.

## File Formats

| Extension | Purpose |
| --- | --- |
| `.syx` | Compatible 32-voice SysEx bank |
| `.opalinevoice` | One Opaline voice |
| `.opalinelibrary.xml` | Multi-bank Opaline voice library |
| `.opalinefmstate` | Plugin-standalone complete state |

## Documentation

- [Technical specification](docs/OpalineFM_Spec.md)
- [Japanese technical specification](docs/OpalineFM_Spec_ja.md)
- [Japanese README](README_ja.md)

## Legal and Project Status

Opaline FM is unofficial and is not affiliated with Yamaha Corporation. Product names are used only to describe file-format and synthesis compatibility.

Do not redistribute third-party factory banks unless their redistribution rights are confirmed. Binary distribution must comply with the selected JUCE license and include applicable VST3 SDK and third-party notices.

The bundled `assets/factory.syx` bank contains original Opaline FM factory patches created for this project. See [NOTICE.md](NOTICE.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before redistributing source or binaries.

The project remains under active development. Audio-thread locking, exact hardware behavior, release signing, notarization, and packaging continue to be reviewed.
