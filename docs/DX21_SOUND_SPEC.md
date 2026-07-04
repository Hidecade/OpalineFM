# DX21 Sound Engine Specification for picoX21H

This document summarizes the DX21-related sound engine behavior implemented in `picoX21H`.

The focus of this document is the current source implementation, not a complete specification of the original Yamaha DX21 hardware.

## 1. Project Overview

`picoX21H` is a hybrid software/hardware simulation of the Yamaha DX21 using the following main components:

- Raspberry Pi Pico / Pico 2
- Yamaha YM2151 / OPM sound chip
- I2S DAC output
- USB MIDI input
- Physical MIDI input
- 16x2 LCD display
- Optional software chorus processing

The original Yamaha DX21 used a YM2164-family 4-operator FM sound engine. This project instead uses the related but different YM2151 chip. Therefore, the current implementation should be understood as a DX21 patch-to-YM2151 conversion engine, rather than a bit-accurate DX21 clone.

## 2. Current Implementation Status

The current implementation can:

- Load the 128 preset voices from the DX21 ROM v1.5 table.
- Select voices via MIDI program handling.
- Play notes through the YM2151.
- Map a subset of DX21 voice parameters to YM2151 registers.
- Apply basic output volume and balance control.
- Apply a simple BBD-style chorus effect through software audio processing.
- Display the selected patch number, patch name, algorithm, and feedback on the LCD.

The current implementation does not yet fully reproduce all DX21 voice parameters.

## 3. Sound Engine Architecture

### 3.1 High-level signal path

```text
MIDI input
  -> MIDI::Instrument voice allocation
  -> DX21::Synth
  -> DX21 ROM voice table
  -> DX21 packed voice decode
  -> YM2151 register programming
  -> YM2151 audio output
  -> DX21::Audio post-processing
  -> DAC output
```

### 3.2 Main sound components

| Component | Role |
|---|---|
| `DX21::Synth` | Main DX21 instrument wrapper. Handles voice programming, note on/off, and MIDI CC handling. |
| `SysEx::Packed` | Packed DX21 voice format as stored in the ROM table. |
| `SysEx::Voice` | Unpacked voice structure used by the synth. |
| `YM2151::Hardware` | YM2151 hardware interface when built for hardware. |
| `YM2151::Emulator` | Stub emulator used when YM2151 hardware is not enabled. |
| `DX21::Audio` | Post-YM2151 audio processing: balance, volume, chorus. |
| `BBD` | Bucket Brigade Delay simulation used by chorus. |
| `iG10090` | BBD modulator simulation used for chorus modulation. |

## 4. Polyphony and Operator Model

### 4.1 Voice count

The synth is configured as an 8-voice MIDI instrument.

```cpp
MIDI::Instrument(/* N */ 8)
```

The YM2151 interface is also configured for 8 voices.

### 4.2 Operator count

The DX21 voice format uses four operators.

```cpp
static const unsigned NUM_OP = 4;
```

The YM2151 also provides four operators per channel.

### 4.3 Operator mapping

The current code maps the four DX21 operator slots to the YM2151 operators as follows:

| DX21 internal index | YM2151 operator |
|---:|---|
| 0 | `OP_M1` |
| 1 | `OP_M2` |
| 2 | `OP_C1` |
| 3 | `OP_C2` |

The debug display prints the internal operator index in reverse order, using `4 - n`, so care is required when comparing internal array order to DX21-style operator numbering.

## 5. Clocking and Sample Rate

The YM2151 clock is currently set to:

```text
3,579,545 Hz
```

The audio sample rate is derived as:

```text
sample_rate = YM2151 clock / 64
```

Therefore, with the current clock:

```text
3,579,545 / 64 = approximately 55,930 Hz
```

## 6. DX21 Voice Data

### 6.1 Voice source

The DX21 preset voices are stored in:

```text
Source/DX21/rom/dx21_rom_v1_5.bin
```

The build system autogenerates a C++ table from this binary:

```cmake
autogen_py(Table_dx21_rom ${CMAKE_CURRENT_SOURCE_DIR}/rom/dx21_rom_v1_5.bin)
```

The synth reads a voice from this table by indexing into packed voice records.

### 6.2 Packed voice format

The packed DX21 voice format includes:

| Category | Parameters |
|---|---|
| Operator count | 4 operators |
| Name | 10 ASCII characters |
| Amplitude EG | AR, D1R, D2R, RR, D1L |
| Operator output | Output Level |
| Frequency data | Encoded in `freq_stuff` |
| Algorithm | `alg` |
| Feedback | `fb` |
| LFO | speed, delay, PMD, AMD, wave, sync |
| Pitch | transpose, pitch bend range, pitch EG |
| Performance controls | portamento, foot volume, modulation wheel, breath controller |
| Chorus | chorus switch |

### 6.3 Unpacked voice format

Important correction: `SysEx::Voice` declares many fields, but the current
assignment from `SysEx::Packed` only copies a small subset.

Actually copied from packed data:

- operator EG: `AR`, `D1R`, `D2R`, `RR`, `D1L`
- operator output level
- simplified operator frequency value: `freq = packed.freq_stuff >> 2`
- algorithm
- feedback
- 10-character voice name
- raw pitch EG block

Declared but not currently decoded/copied into active voice fields:

- `level_scale`
- `rate_scale`
- `eg_bias_sens`
- `a_mod_sens`
- `key_vel`
- `detune`
- `lfo_speed`
- `lfo_delay`
- `lfo_pmd`
- `lfo_amd`
- `lfo_sync`
- `lfo_wave`
- `p_mod_sens`
- `a_mod_sens`
- `transpose`
- `poly_mode`
- `porta_time`
- `foot_volume`
- `foot_sustain`
- `foot_porta`
- `bc_pitch`
- `bc_amp`
- `bc_pitch_bias`
- `bc_eg_bias`

Some of these values exist in `SysEx::Packed`, but they are not assigned by
`Voice::operator=(const Packed&)`, and therefore must not be treated as working
sound behavior.

## 7. Implemented Voice Parameter Mapping

The current `voiceProgram()` implementation maps the following DX21 voice parameters to YM2151 registers.

### 7.1 Operator envelope mapping

| DX21 parameter | YM2151 parameter | Conversion |
|---|---|---|
| `AR` | `EG_AR` | Direct |
| `D1R` | `EG_D1R` | Direct |
| `D1L` | `EG_D1L` | `15 - D1L` |
| `D2R` | `EG_D2R` | Direct |
| `RR` | `EG_RR` | Direct |

### 7.2 Operator output level mapping

DX21 output level is mapped to YM2151 total level:

```cpp
YM2151_TL = (99 - DX21_output_level) * 127 / 99;
```

This inversion is necessary because DX21 output level increases loudness, while YM2151 total level increases attenuation.

### 7.3 Frequency mapping

The current frequency mapping extracts a simplified frequency value from `freq_stuff`:

```cpp
freq = packed.freq_stuff >> 2;
```

This value is then written to the YM2151 `MUL` field.

`YM2151::Interface::setOp<MUL>()` writes only four bits, so the effective value
on the chip is:

```text
MUL = (packed.freq_stuff >> 2) & 0x0F
```

This is a simplified mapping. It does not fully decode DX21 ratio/fixed
frequency mode, coarse/fine behavior, or detune. It also ignores the lower two
bits of `freq_stuff` and all of `ksr__dt1`.

### 7.4 Algorithm and feedback mapping

| DX21 parameter | YM2151 parameter | Conversion |
|---|---|---|
| `alg` | `CONECT` | Direct |
| `fb` | `FB` | Direct |

The LCD displays algorithm and feedback using:

```text
A<algorithm> F<feedback>
```

Note: the debug print displays `alg + 1`, while the LCD and YM2151 register
programming use the raw `alg` value. For a DX21 UI this means care is required
when converting between user-facing 1-8 algorithm numbers and YM2151 0-7
connection values.

## 8. YM2151 Register Interface

The YM2151 interface provides wrappers for chip, channel, and operator parameters.

### 8.1 Global YM2151 parameters

Implemented wrapper parameters include:

- Test
- Noise frequency
- Noise enable
- Timer A / B
- Timer load / reset / IRQ
- CSM
- LFO frequency
- LFO AMD
- LFO PMD
- LFO waveform
- CT pins

### 8.2 Channel parameters

| Parameter | Meaning |
|---|---|
| `CONECT` | Operator connection / algorithm |
| `FB` | Feedback |
| `RL` | Left/right output enable |
| `KC` | Key code |
| `KF` | Key fraction |
| `AMS` | Amplitude modulation sensitivity |
| `PMS` | Pitch modulation sensitivity |

### 8.3 Operator parameters

| Parameter | Meaning |
|---|---|
| `MUL` | Frequency multiplier |
| `DT1` | Detune 1 |
| `DT2` | Detune 2 |
| `EG_TL` | Total level |
| `EG_AR` | Attack rate |
| `EG_D1R` | First decay rate |
| `EG_D2R` | Second decay rate |
| `EG_RR` | Release rate |
| `EG_D1L` | First decay level |
| `AMS_EN` | Amplitude modulation enable |
| `KS` | Key scaling |

The interface can write more YM2151 parameters than the DX21 conversion code currently uses.

## 9. MIDI Behavior

### 9.1 MIDI inputs

The project supports:

- USB MIDI interface
- Physical MIDI interface

Both are attached to MIDI channel 1 in the main program.

### 9.2 Note on

On note-on:

1. The MIDI note number is converted to a YM2151 key code.
2. The YM2151 channel key code register is written.
3. All four operators are keyed on.

Velocity is accepted by the function signature, but `YM2151::Interface::voiceOn()`
does not use it. The current implementation does not apply velocity to operator
level or envelope behavior.

### 9.3 Note off

On note-off, the YM2151 key-on register is written with only the voice number, causing the channel to key off.

### 9.4 MIDI control change

Current CC handling:

| MIDI CC | Function |
|---:|---|
| 7 | Output volume |
| 8 | Balance |
| 12 | Chorus enable when value is 64 or greater |
| 119 | Program selection workaround |

CC119 is described in code as a workaround for DAWs that make program selection inconvenient.

### 9.5 Not yet implemented MIDI performance controls

The following MIDI/performance controls are stubbed or not currently applied:

- Polyphonic/channel pressure
- Pitch bend
- Velocity sensitivity
- Mod wheel pitch modulation
- Mod wheel amplitude modulation
- Breath controller controls
- Foot controller controls
- Sustain behavior
- Portamento behavior

## 10. Audio Post-processing

The `DX21::Audio` class processes the mixed YM2151 output before the DAC.

### 10.1 Balance

The dry left and right signals are computed as:

```cpp
dry_l = mix.left  * (128 - balance) / 64;
dry_r = mix.right * balance         / 64;
```

Default balance is 64.

### 10.2 Volume

The final output is scaled by:

```cpp
mix.left  = mix_l * volume / 128;
mix.right = mix_r * volume / 128;
```

Default volume is 127.

The code contains a TODO note for logarithmic volume handling.

### 10.3 Chorus

When chorus is enabled:

1. The BBD modulation value is obtained from `iG10090::sample()`.
2. The BBD receives the mono-summed dry signal.
3. The BBD output is mixed back into the stereo signal with opposite polarity between left and right.

Current chorus mixing:

```cpp
wet = bbd.sendRecv((dry_l + dry_r) / 2);
mix_l = (dry_l + wet) / 2;
mix_r = (dry_r - wet) / 2;
```

### 10.4 BBD implementation

The chorus uses:

```cpp
BBD</* LOG2_SIZE */ 8>
```

This corresponds to a 256-sample bucket buffer.

The BBD implementation uses:

- Input interpolation buffer
- Bucket delay buffer
- Output interpolation buffer
- Filter table `table_bbd_filter`
- Modulated input/output phase increments

### 10.5 iG10090-style modulator

The `iG10090` class simulates a BBD modulator using:

- Sine lookup table
- Two LFOs
- Tremolo component
- Chorus component
- First-order Butterworth low-pass filter around 200 Hz

The constructor currently uses:

```cpp
iG10090 modulator{/* clock */ 8000, /* chorus */ 5, /* tremolo */ 18};
```

## 11. LCD Display Behavior

When voice 0 is programmed, the synth prints debug information and updates the 16x2 LCD.

LCD row 0:

```text
<program number> <10-character voice name>
```

LCD row 1:

```text
A<algorithm> F<feedback>
```

Only index 0 updates the LCD, so the display reflects the primary/current program state rather than every active voice.

## 12. Implemented vs Not Implemented

### 12.1 Implemented or partially implemented

| Feature | Status |
|---|---|
| 8-voice playback | Implemented |
| 4-operator FM structure | Implemented via YM2151 |
| DX21 ROM v1.5 preset table | Implemented |
| Program selection | Implemented |
| MIDI note on/off | Implemented |
| Algorithm mapping | Implemented |
| Feedback mapping | Implemented |
| Operator AR/D1R/D1L/D2R/RR | Implemented |
| Operator output level | Implemented with conversion to YM2151 TL |
| Frequency multiplier | Partially implemented |
| Volume CC | Implemented |
| Balance CC | Implemented |
| Chorus CC | Implemented |
| BBD chorus | Implemented but still experimental/noisy |
| LCD display | Implemented |

### 12.2 Defined but not fully implemented

| Feature | Status |
|---|---|
| LFO speed | Defined but not applied |
| LFO delay | Defined but not applied |
| LFO waveform | Defined but not applied |
| PMD | Defined but not applied |
| AMD | Defined but not applied |
| Pitch EG | Copied as a raw block but not applied |
| Transpose | Defined but not applied |
| Pitch bend range | Defined but not applied |
| Pitch bend | Stubbed |
| Velocity sensitivity | Defined but not applied |
| Key velocity | Defined but not applied |
| Key scaling | Defined but not applied |
| Rate scaling | Defined but not applied |
| EG bias sensitivity | Defined but not applied |
| Amplitude modulation sensitivity | Defined but not applied in voice conversion |
| Detune | Defined but not applied |
| Portamento | Defined but not applied |
| Foot controller | Defined but not applied |
| Breath controller | Defined but not applied |
| Patch chorus switch | Defined but not applied automatically |
| Sustain | Defined but not applied |
| Poly/mono mode | Defined but not applied |

## 13. Known Technical Gaps

The following areas are important for improving DX21 accuracy:

1. **YM2164 vs YM2151 differences**
   - The project uses YM2151 even though the DX21 used a YM2164-family chip.
   - Register and behavior differences need systematic verification.

2. **Complete DX21 packed voice decoding**
   - `freq_stuff`, `ksr__dt1`, and combined sensitivity fields need full bit-level decoding.

3. **Operator frequency accuracy**
   - Current implementation only extracts `freq_stuff >> 2` and writes it to `MUL`.
   - Full DX21 fixed/ratio frequency behavior is not yet represented.

4. **Detune and scaling**
   - Detune, key scaling, and rate scaling are critical for closer patch reproduction.

5. **Modulation system**
   - DX21 LFO parameters should be mapped to YM2151 LFO registers where possible.
   - PMS/AMS and operator AMS enable need to be derived from the DX21 patch.

6. **Pitch EG**
   - Pitch EG data exists in the voice structure but is not yet applied.

7. **Velocity and controller response**
   - Velocity, breath controller, foot controller, and modulation wheel behavior are not yet implemented.

8. **Chorus tuning**
   - The BBD chorus exists but is still experimental and noisy.

9. **Patch-level chorus switch**
   - Patch data includes a chorus switch, but chorus is currently controlled by MIDI CC12 rather than automatically by voice data.

## 14. Recommended Development Roadmap

### Phase 1: Complete voice decoding

- Fully decode `freq_stuff`.
- Fully decode `ksr__dt1`.
- Fully decode `amp_mod__eg_bias__sens__key_vel`.
- Verify packed voice byte layout against the DX21 service manual / SysEx specification.

### Phase 2: Improve YM2151 mapping

- Map DX21 detune to YM2151 `DT1` / `DT2` where possible.
- Map key scaling to YM2151 `KS`.
- Map amplitude modulation sensitivity to `AMS` / `AMS_EN`.
- Map pitch modulation sensitivity to `PMS`.
- Apply LFO speed, waveform, PMD, and AMD.

### Phase 3: Improve performance behavior

- Apply MIDI velocity to operator levels according to DX21 velocity sensitivity.
- Implement pitch bend range.
- Implement modulation wheel pitch and amplitude modulation.
- Implement breath controller behavior.
- Implement foot controller behavior.
- Implement portamento.

### Phase 4: Improve DX21-specific behavior

- Implement pitch EG.
- Implement patch-level chorus switch.
- Investigate dual/split/performance-level DX21 behavior if required.
- Compare output against a real DX21 using captured audio and SysEx patch data.

### Phase 5: Chorus and output refinement

- Tune BBD depth and rate.
- Reduce chorus noise.
- Implement logarithmic volume response.
- Verify stereo balance behavior against the intended hardware behavior.

## 15. Summary

The current `picoX21H` sound engine is best described as an experimental DX21-inspired 4-operator FM synthesizer that loads DX21 ROM voices and maps a core subset of their parameters to a YM2151 sound chip.

It already implements the essential path needed to select and play DX21 preset patches:

```text
DX21 ROM voice -> partial SysEx decode -> YM2151 register setup -> MIDI playback -> audio post-processing
```

However, many DX21-specific expressive and modulation parameters are currently only defined in data structures and are not yet applied to the sound chip. For closer real-DX21 reproduction, the highest-priority work is complete packed-voice decoding, more accurate frequency/detune/scaling conversion, LFO/Pitch EG implementation, and MIDI performance control support.

---

## 16. Source Review Corrections

The following corrections were made after reviewing the picoX21H source directly.

### 16.1 `SysEx::Voice` fields vs active behavior

The source declares many fields in `SysEx::Voice`, but most are not populated
from `SysEx::Packed`.

The active unpack path copies only:

- operator EG
- operator output level
- simplified operator frequency
- algorithm
- feedback
- name
- raw pitch EG

The following should be considered not implemented, even though fields exist:

- LFO speed/delay/PMD/AMD/sync/wave
- PMS/AMS
- operator velocity sensitivity
- operator detune
- key scaling
- rate scaling
- EG bias sensitivity
- transpose
- pitch bend range
- chorus switch
- portamento
- foot controller
- breath controller
- sustain
- poly/mono mode

### 16.2 Effective frequency conversion is narrower than it first appears

The code first computes:

```cpp
freq = packed.freq_stuff >> 2;
```

But the YM2151 register writer stores `MUL` as a 4-bit field. Therefore the
effective chip value is:

```text
MUL = (packed.freq_stuff >> 2) & 0x0F
```

This means picoX21H currently discards substantial DX21 frequency information.
It is useful as a quick YM2151 approximation, but it is not suitable as a
DX21-accurate frequency model for DX21Native.

### 16.3 YM2151-capable parameters are not necessarily used

`YM2151::Interface` can write useful OPM parameters such as:

- `DT1`
- `DT2`
- `KS`
- `AMS_EN`
- global `LFO_FRQ`
- global `LFO_AMD`
- global `LFO_PMD`
- global `LFO_WAVE`
- channel `AMS`
- channel `PMS`

However, `DX21::Synth::voiceProgram()` currently does not derive these from the
DX21 patch. It forces channel `AMS` and `PMS` to `0`.

### 16.4 Algorithm numbering mismatch

Three representations exist:

- `SysEx::Packed::alg`: raw 0-7
- debug print: `alg + 1`
- LCD and YM2151 register: raw `alg`

For DX21Native, keep the UI as 1-8 but keep internal algorithm indexing
explicit. Avoid hidden off-by-one conversions.

### 16.5 Velocity is ignored

`voiceOn(index, midi_note, velocity)` receives velocity, but the YM2151 key-on
path ignores it. This confirms that picoX21H does not currently implement DX21
key velocity sensitivity.

### 16.6 Patch chorus switch is not used

`SysEx::Packed` contains `chorus_switch`, but the current chorus on/off behavior
is controlled by MIDI CC12:

```cpp
audio.chorus = value_ >= 64;
```

## 17. Notes for DX21Native Engine Improvements

picoX21H is valuable as an OPM/YM2151 conversion reference, but should not be
copied directly if the goal is closer DX21 behavior.

Useful references:

- YM2151 total level is attenuation, so it moves opposite to DX21 output level.
- picoX21H maps DX21 output level to YM2151 TL as `(99 - level) * 127 / 99`.
- YM2151 D1L is programmed as `15 - DX21_D1L`.
- OPM algorithm values are naturally 0-7, while DX21 UI values are usually 1-8.
- The BBD chorus structure is useful: 256 bucket buffer, modulated delay, mono
  send, opposite-polarity stereo return.

Do not copy directly:

- `freq_stuff >> 2` is too lossy for DX21Native.
- `MUL` alone cannot represent DX21 ratio/fixed frequency behavior.
- Forcing `AMS` and `PMS` to zero removes important modulation behavior.
- Ignoring velocity loses a major MIDI performance control.
- Ignoring patch chorus switch loses patch-level behavior.

Recommended DX21Native priorities:

1. Keep full DX21 VMEM frequency decode instead of reducing to YM2151 `MUL`.
2. Continue refining detune with a DX21-specific model, not only OPM `DT1/DT2`.
3. Verify D1L, rate scaling, and key scaling against DX21 behavior.
4. Keep per-operator velocity sensitivity active.
5. Implement or refine LFO PMD/AMD/PMS/AMS and operator AM enable.
6. Implement pitch EG if closer DX21 reproduction is required.
7. Use picoX21H chorus as inspiration, but tune gain/noise/depth independently.
