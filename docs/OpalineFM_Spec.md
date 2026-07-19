# Opaline FM Specification

Opaline FM is a 4-operator FM synthesizer implemented in C++/JUCE. It is inspired by classic 1980s 4-op digital FM instruments and focuses on practical compatible voice-bank handling. It uses FM synthesis, but it is not a chip emulator and does not attempt to perfectly reproduce any specific FM hardware instrument.

Detailed research notes are kept outside the repository in a private documentation folder. This file is the repository-facing summary for development, release notes, and public documentation.

For installation and operation, see the [English README](../README.md) or [Japanese README](../README_ja.md). This document defines engine behavior, parameter ownership, file compatibility, and implementation constraints for developers and maintainers.

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
- The public rendering path handles operator level, attenuation, feedback, carrier mixing, and output behavior.

The internal names still use `opaline` because they describe compatibility-level data structures and SysEx semantics.

### Real-Time Safety

- Desktop and iOS audio render paths do not acquire the mutex used to protect UI and library snapshots.
- Note On/Off, Pitch Bend, Mod Wheel, Sustain, Portamento Foot Switch, and Panic are delivered to the audio thread through a fixed-capacity `RealtimeCommandQueue`.
- Complete voice, effect, and performance settings are published through a fixed-slot `RealtimeStateMailbox` and applied at buffer boundaries.
- The audio render path is the only consumer of the queue and mailbox; the UI thread never executes queued real-time commands.
- During WAV recording, the audio thread writes only to a preallocated ring buffer. Buffer growth, collection, and file output happen on a collector thread or the UI side.
- The desktop audio callback does not query the audio device or compare device-name strings. It reads an atomic output-trim value determined when the device starts.

### iOS Audio Lifecycle

The iOS standalone app observes output-route changes, `AVAudioSession.interruptionNotification`, `mediaServicesWereResetNotification`, and SwiftUI `scenePhase`. Audio is suspended during interruptions or background transitions. On recovery, the app reacquires the session sample rate and route and rebuilds `AVAudioEngine`. Phone, Siri, Bluetooth, and headphone recovery remain part of physical-device regression testing.

## Four-Operator FM Architecture

Each voice contains four sine-wave operators. An operator produces audio at `baseFrequency * ratio + detune`. The algorithm table determines whether that output is sent to the final carrier mixer or used as phase modulation for another operator.

- A **carrier** contributes directly to audible output.
- A **modulator** changes the phase of a downstream operator and therefore changes harmonic content.
- Operator LEVEL controls carrier amplitude or modulator index according to the operator's role.
- Operator 4 owns the feedback loop. FB selects the feedback amount.
- Algorithms 1-4 mainly use serial modulation chains; algorithms 5-8 introduce parallel branches and additional carriers.
- Parallel carriers are summed before voice output processing. A mild carrier-count compensation is applied as described below.

Per-operator signal order:

```text
note + transpose + bend + PEG + pitch LFO + portamento
    -> ratio/detune oscillator
    -> phase modulation and OP4 feedback
    -> amplitude EG
    -> LEVEL / LevelSc / velocity
    -> AM (when enabled)
    -> carrier output or downstream modulation bus
```

Voice outputs are mixed, limited/declicked, and then passed through chorus, delay, reverb, wet-mix, and tone processing.

### Algorithm routing table

The implementation stores operators as `0..3`, but this specification uses the user-facing `OP1..OP4` names. `>` means that the left operator phase-modulates the right operator, and `+` means parallel summing.

| ALG | Routing | Carriers |
| --- | --- | --- |
| 1 | `OP4 > OP3 > OP2 > OP1` | OP1 |
| 2 | `(OP4 + OP3) > OP2 > OP1` | OP1 |
| 3 | `OP3 > OP2 > OP1` and `OP4 > OP1` | OP1 |
| 4 | `OP2 > OP1` and `OP4 > OP3 > OP1` | OP1 |
| 5 | `OP2 > OP1` and `OP4 > OP3` | OP1, OP3 |
| 6 | `OP4 > (OP1 + OP2 + OP3)` | OP1, OP2, OP3 |
| 7 | `OP4 > OP3`, `OP1`, `OP2` | OP1, OP2, OP3 |
| 8 | `OP1 + OP2 + OP3 + OP4` | OP1, OP2, OP3, OP4 |

### Phase-Modulation Math

The renderer separates each operator result into an `audio` value and a `modulation bus` value used to phase-modulate downstream operators. The `modulation bus` is treated as an integer bus and is evaluated from deeper modulators toward carriers on every sample.

Main constants:

```text
phaseSteps = 1048576
sineIndexSteps = 1024
operatorBusPeak = 8192
tlSubsteps = 8
tlMax = 127
logAttenuationDbPerStep = 6.020599913279624 / 256
```

Operator frequency and phase advance:

```text
ratio = ratioTable[ratioIndex]
frequency = baseFrequency * ratio + dt1Offset(baseFrequency, ratio, detune, midiNote)
phaseIncrement = round(clamp(frequency, 0, sampleRate * 0.49) * phaseSteps / sampleRate)
phase += clamp(phaseIncrement, 0, phaseSteps - 1) * 2*pi / phaseSteps
basePhaseIndex = floor(fract(phase / (2*pi)) * sineIndexSteps) & 1023
```

The phase-modulation bus for an operator is the sum of upstream operators' `modulation bus` values, followed by an arithmetic-right-shift equivalent. Negative values use `-((abs(x) + 1) / 2)` to keep integer rounding deterministic.

```text
rawPmBus = sum(upstreamOperator.modulationBus)
pmBus = round(arithmeticShiftRightOne(rawPmBus))
pmBus = clamp(pmBus, -8192, 8191)
pmIndex = round(pmBus)
```

Only OP4 has feedback. The previous two OP4 `modulation bus` samples are summed, halved with the same arithmetic shift, and converted to a phase-index offset according to FB.

```text
feedbackBus = arithmeticShiftRightOne(op4History[0] + op4History[1])

if FB == 0:
    feedbackIndex = 0
else:
    feedbackIndex = round(clamp(round(feedbackBus), -8192, 8191) / 2 ^ (9 - FB))
```

The final sine-table position wraps at 1024 steps.

```text
phaseIndex = (basePhaseIndex + pmIndex + feedbackIndex) & 1023
```

The renderer does not compute the operator waveform with a direct `sin()` call. It maps the 1024-step phase index to sign and log-sine attenuation, adds envelope, TL, velocity, and AM attenuation, then converts the total attenuation back to an audio value through the exponential table.

```text
scaledLevel = clamp(level - floor(LevelSc * 2 ^ (midiNote / 12) / 384), 0, 99)
targetTl = scaledLevel <= 0 ? 127 : round(99 - scaledLevel)
currentTl = smoothed(targetTl)

velocityDb = -20 * log10(velocityFactor)
ampModDb = AM ? -20 * log10(ampModFactor) : 0
attenuationIndex = clamp(envelopeIndex
                         + currentTl * tlSubsteps
                         + (velocityDb + ampModDb) / logAttenuationDbPerStep,
                         0, 1023)

waveAttenuation = logSinRom(phaseIndex)
egAttenuation = round(attenuationIndex) << 2
attenuation = clamp(waveAttenuation + egAttenuation, 0, 4095)
audio = sign(phaseIndex) * expRom(attenuation) / 8192
modulationBus = clamp(round(audio * operatorBusPeak), -8192, 8191)
```

`targetTl` is not stepped instantly. It follows toward `currentTl` through an approximately `0.012 s` ramp to reduce clicks when LEVEL or LevelSc changes.

## Parameter Ownership

Voice parameters are stored in `OpalinePatch` and travel with compatible voice data where the source format supports them. These include operator settings, algorithm, feedback, Pitch EG, LFO, transpose, and effects.

Performance parameters are stored in application/plugin state rather than in a SysEx voice:

- SINGLE/DUAL/SPLIT mode and A/B voice selection
- A/B POLY or MONO mode
- DUAL detune, SPLIT point, and A/B balance
- Pitch Bend RANGE and PORTA
- MOD PITCH and MOD AMP wheel ranges
- Master volume and render model

Selecting voice A initializes MOD PITCH and MOD AMP from its PMD and AMD. Subsequent wheel-range edits remain performance state until another A voice is selected.

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
PR 99 : short audible transition, about 12 ms
```

High-rate values around PR90-98 use measured correction data. PR99 is not a true mathematical zero; it is implemented as a short audible transition so plucked/string-like pitch attacks keep a small "put" transient.

## Keyboard Level Scaling

Keyboard Level Scaling (`LevelSc`) follows separate DX21 measurements of carrier and modulator operators. Both operator roles produce the same scaling values, so they share one measured table. MIDI note numbers are normative; octave names such as C0 or C1 are not used as the specification because their naming differs between hosts.

The implementation uses the table below as lookup anchors instead of relying on a single approximate expression. Values between note anchors follow a curve that approximately doubles per octave, while intermediate `LevelSc` values are interpolated linearly between adjacent measured rows. The result is rounded to the nearest integer and clamped to `0..127`. Notes below 36 and above 96 are extrapolated with the same octave ratio.

`LevelSc` and Rate Scaling use the sounding key after Transpose has been added to the received MIDI note. For example, MIDI note 60 with Transpose -12 uses note 48 for oscillator pitch, `LevelSc`, and Rate Scaling. It therefore has the same timbre as MIDI note 48 with Transpose 0. This matches the DX21 behavior in which changing Transpose does not alter the voice timbre.

The detune frequency table uses the same transposed sounding key.

Measured TL-equivalent attenuation at MIDI notes 36, 48, 60, 72, 84, and 96:

```text
LevelSc  36  48  60  72  84  96
      0   0   0   0   0   0   0
     25   0   1   2   4   8  16
     50   1   2   4   8  16  33
     75   1   3   6  12  25  50
     99   1   3   7  16  33  67
```

## Keyboard Rate Scaling Measurement

The following four voices are used to measure DX21 Rate Scaling:

- `assets/DX21_RateScale_RS0.opalinevoice`
- `assets/DX21_RateScale_RS1.opalinevoice`
- `assets/DX21_RateScale_RS2.opalinevoice`
- `assets/DX21_RateScale_RS3.opalinevoice`

Each voice uses only OP1 as a carrier, with `AR=31`, `D1R=10`, `D1L=0`, and `LevelSc=0`. AR remains instantaneous so it is excluded from the measurement, while the slower D1 decay provides enough time resolution. This isolates RateSc from level scaling and modulation.

1. Load one measurement voice in Opaline FM.
2. Send it to the DX21 with `VOICE EXPORT`.
3. Start recording and run the internal `MIDI KEY TEST` measurement function.
4. Repeat for RateSc 0, 1, 2, and 3.

MIDI Key Test plays notes `0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 120, 127` for three seconds each. The button is hidden from the normal v1.0.9 UI. Name the recordings `DX21_RateScale_RS0.wav` through `DX21_RateScale_RS3.wav`. Compare the elapsed time from each note onset to a fixed attenuation threshold, such as 30 dB below peak, to derive the measured table.

D1 decay time in seconds from peak to `-20 dB`, measured from a DX21 recording on July 19, 2026:

```text
Note     0     12     24     36     48     60     72     84     96    108    120    127
RS 0  1.460  1.455  1.430  1.430  1.150  1.170  0.975  0.975  0.840  0.840  0.840  0.840
RS 1  1.455  1.455  1.415  1.145  0.960  0.825  0.735  0.585  0.490  0.490  0.490  0.420
RS 2  1.455  1.485  1.140  0.825  0.580  0.415  0.290  0.215  0.150  0.155  0.150  0.105
RS 3  1.455  1.455  0.785  0.395  0.200  0.110  0.055  0.030  0.020  0.015  0.015  0.010
```

For RS 3, decay time approximately halves per octave above note 24. RS 0 is not fully disabled; it retains a weak key-rate change in the upper range. This behavior agrees with the current key-code model:

```text
keyScaleCode = clamp(round((transposedNote - 24) / 3), 0, 31)
rateOffset = keyScaleCode >> (RateSc xor 3)
```

One TL step corresponds to approximately `0.752575 dB`. The renderer subtracts the integer scaling amount from the operator's `0..99` LEVEL first, clamps the effective LEVEL to zero, and then converts it to TL. This ensures that strong scaling can fully silence a low-level operator at high notes.

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

### Modulation wheel ranges

- `MOD PITCH` is `0-99` and scales modulation-wheel pitch depth before PMS is applied.
- `MOD AMP` is `0-99` and scales modulation-wheel amplitude depth before AMS is applied.
- Amplitude modulation only affects operators whose AM switch is enabled.
- Direct LFO PMD/AMD and modulation-wheel ranges are independent; their effective depths are added and clamped to `99`.
- Selecting voice A initializes `MOD PITCH` from that voice's PMD and `MOD AMP` from its AMD. The ranges can then be edited independently.
- Selecting voice B does not overwrite these shared modulation-wheel ranges.

### Portamento mode

- Each A/B engine stores an independent OFF/FULL/FINGER portamento mode.
- `POLY` exposes OFF/FULL. `MONO` exposes OFF/FULL/FINGER.
- FULL glides from the last played note. FINGER glides only when the next note is pressed while another key is still held.
- Sustain-pedal-held notes do not count as physically held keys for Fingered Portamento.
- MIDI `CC65` controls the Portamento Foot Switch in `POLY` mode: values `64-127` enable Full Time Portamento and `0-63` disable it.
- MIDI `CC65` does not gate portamento in `MONO` mode because Fingered Portamento is controlled by overlapping held keys.

### Parallel carrier level compensation

- The carrier mixer applies a mild `carrierCount^-0.18` gain.
- Approximate trims are `0 dB` for one carrier, `-1.1 dB` for two, `-1.7 dB` for three, and `-2.2 dB` for four.

## Performance Controls

- Pitch Bend Range is `0..12` semitones and defaults to `2`. A range of `12` bends up or down by one octave at the wheel limits.
- Portamento is `0..99`; zero is the shortest glide time and does not disable the effect.
- Portamento values `1..99` use a nonlinear time curve from approximately 10 ms to 2 seconds. A newly triggered voice starts at the previous played note and glides to its target note.
- RANGE, PORTA time, and the per-engine portamento modes are performance settings, not voice parameters, and are stored in standalone/plugin state.

## MIDI Control

| MIDI message | Engine behavior |
| --- | --- |
| Note On/Off | Starts or releases notes according to SINGLE/DUAL/SPLIT and POLY/MONO state |
| Pitch Bend | Bipolar bend using the global `0..12` RANGE |
| CC1 | Modulation wheel; drives MOD PITCH and MOD AMP paths |
| CC64 | Sustain pedal; Note Off is deferred while the pedal is down |
| CC65 | Portamento foot switch in POLY mode; ignored as a gate in MONO/Fingered mode |
| All Notes Off / All Sound Off | Clears active voices and controller-held notes |

The plugin-standalone wrapper accepts PC-keyboard note input only while its window has focus. The VST3 editor does not map PC typing keys to notes, preserving DAW keyboard shortcuts. Both wrappers accept host/MIDI notes and on-screen keyboard input.

## Plugin State

DAWs save plugin state via `getStateInformation()` / `setStateInformation()` using a JUCE `ValueTree`.

The plugin standalone wrapper also exposes:

- `Save current state...`
- `Load a saved state...`
- `Reset to default state`

The desired state suffix is `.opalinefmstate`.

## Release Notes and Legal Notes

The v1.0.9 public release provides the following installer formats:

- Windows x64 standalone installer: `OpalineFM-Standalone-v1.0.9-Windows-x64.exe`
- Windows x64 VST3 installer: `OpalineFM-VST3-v1.0.9-Windows-x64.exe`
- Signed and notarized macOS standalone, VST3, and Audio Unit packages

The Windows VST3 installer targets `C:\Program Files\Common Files\VST3\Opaline FM.vst3`. Windows installers are currently unsigned; macOS packages are signed and notarized. Published installers must be obtained from the [official GitHub Release](https://github.com/Hidecade/OpalineFM/releases/tag/v1.0.9).

Public release documentation should state:

- Opaline FM uses FM synthesis, but it is not a chip emulator and does not attempt to perfectly reproduce any specific FM hardware instrument.
- Compatible product names, if mentioned, are used only to describe compatibility.
- `assets/factory.syx` is an original Opaline FM factory bank created for this project.
- Third-party factory voice banks are not redistributed unless rights are confirmed.
- JUCE licensing must be satisfied before binary distribution.
- VST3 SDK notices and other third-party notices should be included.

## Known Technical Debt

- The plugin state and APVTS sync path is practical but not fully sample accurate.
- Dual/Split behavior and exact manual LFO sharing rules are not complete.
- Per-sample `pow` calls, voice-retirement scans, and effect processing remain in the engine. Any precomputation or zero-setting short circuit should follow the addition of audio-output regression comparisons.
- iOS interruption recovery and AUv3 support are implemented, but physical-device regression testing must continue for phone calls, Siri, Bluetooth changes, and long sessions.
- The JUCE submodule has been locally modified for standalone state suffix behavior; for public release this should either be managed as a fork or replaced with a non-invasive solution.
