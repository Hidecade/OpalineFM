# Opaline FM Specification

Opaline FM is a 4-operator FM synthesizer implemented in C++/JUCE. It is inspired by classic 1980s 4-op digital FM instruments and focuses on practical compatible voice-bank handling without presenting itself as an official Yamaha product.

Detailed research notes are kept outside the repository in a private documentation folder. This file is the repository-facing summary for development, release notes, and public documentation.

## Goals

- Provide a standalone app, VST3 instrument, plugin standalone build, and macOS AU build path.
- Load and save compatible 32-voice SysEx banks.
- Keep the engine usable as both a musical instrument and a reference implementation for measured 4-op FM behavior.
- Keep the public identity as Opaline FM while preserving internal compatible terminology where it describes file formats or parameter compatibility.

## Product Names

- Public product name: `Opaline FM`
- CMake project: `OpalineFM`
- Main plugin target: `OpalineFM_Plugin`
- Standalone app target: `OpalineFM_Standalone`
- Plugin state file suffix: `.opalinefmstate`
- Voice-library export default: `OpalineFM_Voice_Library.opalinelibrary.xml`

## Build Outputs

Windows debug build outputs:

```text
build/standalone-vs-debug/OpalineFM_Plugin_artefacts/Debug/VST3/Opaline FM.vst3
build/standalone-vs-debug/OpalineFM_Plugin_artefacts/Debug/Standalone/Opaline FM.exe
build/standalone-vs-debug/OpalineFM_Standalone_artefacts/Debug/Opaline FM.exe
```

macOS AU build target:

```bash
cmake --preset macos-debug
cmake --build --preset plugin-au-macos-debug
```

Expected AU output:

```text
build/macos-debug/OpalineFM_Plugin_artefacts/Debug/AU/Opaline FM.component
```

## Engine Summary

The audio engine lives under `Source/Engine` and is shared by all frontends.

Main concepts:

- `OpalinePatch` stores voice parameters in compatible ranges.
- `OpalineEngine` manages voices, MIDI note state, pitch bend, mod wheel, and sample rendering.
- `OpalineVoice` renders the operator graph, envelopes, LFO, pitch envelope, feedback, and output quantization/model differences.
- The engine currently has an OLD/current model and a NEW/chip-hybrid model.

The internal names still use `opaline` because they describe compatibility-level data structures and SysEx semantics.

## UI and Hosts

There are two frontend paths:

- Full standalone app: owns audio device and MIDI input selection.
- Plugin editor: reuses `MainComponent` in host mode and lets the DAW/plugin wrapper own audio and MIDI routing.

The plugin editor receives:

- MIDI note display state from the processor.
- Pitch bend and mod wheel state from the processor.
- Selected program name for host display where supported.

## Voice Bank Handling

Opaline FM supports compatible 32-voice bulk SysEx banks.

Supported user-facing file types:

- `.syx`: single compatible 32-voice bank.
- `.opalinelibrary.xml`: Opaline/Opaline-style multi-bank library export.
- `.opalinefmstate`: plugin standalone full state file.

Public releases should avoid bundling copyrighted factory banks unless their redistribution status is confirmed. Users can load their own `.syx` files.

## Pitch Envelope Generator

The Pitch EG is implemented as a separate high-level pitch envelope:

- PL center is `50`.
- PL range is approximately `-4800` to `+4800` cents.
- PR controls transition speed.
- PR1 moves from PL3 to PL1 after key-on.
- PR2 moves from PL1 to PL2.
- PL2 is the held pitch level.
- PR3 moves to PL3 after key-off.

Current measured PR anchors:

```text
PR 0  : 63.55 s
PR 10 : 9.14 s
PR 20 : 5.37 s
PR 30 : 3.58 s
PR 40 : 2.40 s
PR 50 : 1.59 s
PR 60 : 1.13 s
PR 70 : 0.855 s
PR 80 : 0.579 s
PR 90 : 0.416 s
PR 99 : short clock-like transition
```

High-rate values around PR90-98 use measured correction data. PR99 is not a true mathematical zero; it is implemented as a short audible transition.

## LFO and Modulation

Important behavior:

- LFO speed follows measured/manual anchors, including speed 35 near 6.7 Hz and speed 99 near 55 Hz.
- PMD is a direct pitch modulation depth source.
- Mod Wheel pitch is added as a separate MW PITCH path, not as a scaler that reduces PMD.
- PMS=0 follows the Vibrato OSC behavior rather than simple pitch-modulation-off behavior.
- Square PMS=7 / PMD=99 reaches approximately `-8` to `+8` semitones.

Current PMS depth table:

```text
PMS 0: Vibrato OSC, approximately PMS=5 behavior
PMS 1: 0.125 semitones
PMS 2: 0.25 semitones
PMS 3: 0.5 semitones
PMS 4: 1.0 semitones
PMS 5: 2.0 semitones
PMS 6: 4.0 semitones
PMS 7: 8.0 semitones
```

## Plugin State

DAWs save plugin state via `getStateInformation()` / `setStateInformation()` using a JUCE `ValueTree`.

The plugin standalone wrapper also exposes:

- `Save current state...`
- `Load a saved state...`
- `Reset to default state`

The desired state suffix is `.opalinefmstate`.

## Release Notes and Legal Notes

Public release documentation should state:

- Opaline FM is unofficial and not affiliated with Yamaha.
- Yamaha and compatible names, if mentioned, are used only to describe compatibility.
- Factory voice banks are not redistributed unless rights are confirmed.
- JUCE licensing must be satisfied before binary distribution.
- VST3 SDK notices and other third-party notices should be included.

## Known Technical Debt

- Audio-thread locking remains an area for future RT-safety work.
- The plugin state and APVTS sync path is practical but not fully sample accurate.
- Dual/Split behavior and exact manual LFO sharing rules are not complete.
- The JUCE submodule has been locally modified for standalone state suffix behavior; for public release this should either be managed as a fork or replaced with a non-invasive solution.
