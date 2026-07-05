# WebDX21 LFO Specification Extract

Source: `D:\Hidecade\WebDX21`

This document extracts the LFO-related behavior from WebDX21 for use as a reference when improving the DX21Native New engine.

Real hardware observations are tracked separately in `docs/DX21_HARDWARE_INVESTIGATION.md`.

## Source Files

- `src/dx21-worklet.js`: JavaScript FM engine LFO behavior.
- `src/dx21-opm-mapper.js`: DX21 patch to OPM/YM2151 register mapping.
- `src/opm-wasm-worklet.js`: OPM/WASM runtime LFO delay and modulation-wheel scaling.
- `docs/DX21_Manual_Notes.md`: notes on DX21 LFO behavior.
- `docs/DX21_WebSynth_Spec.md`: documented WebDX21 LFO behavior.

## Parameters

DX21 LFO values are kept in the patch as raw DX21 ranges:

| Parameter | Range | Meaning |
|---|---:|---|
| `speed` | 0..99 | LFO speed |
| `delay` | 0..99 | LFO delay before modulation fades in |
| `pitchDepth` / PMD | 0..99 | Pitch modulation depth |
| `ampDepth` / AMD | 0..99 | Amplitude modulation depth |
| `pitchSensitivity` / PMS | 0..7 | Pitch modulation sensitivity |
| `ampSensitivity` / AMS | 0..3 | Amplitude modulation sensitivity |
| `sync` | bool | Per-note LFO phase when true |
| `wave` | 0..3 | 0 Saw Up, 1 Square, 2 Triangle, 3 S/Hold |

## JavaScript FM Path

### LFO Speed

`dx21-worklet.js` maps speed directly to Hz:

```js
normalized = clamp(speed, 0, 99) / 99
if normalized <= 0:
  hz = 0.01
else:
  hz = 55 * normalized ** 2.0246997291383155
```

This is the older JS/FM behavior and reaches about 55 Hz at speed 99.

### LFO Phase Source

```js
lfoAge = patch.lfo.sync ? voice.age : globalLfoAge
lfoPhase = dx21LfoSpeedToHz(patch.lfo.speed) * lfoAge
```

`sync=true` restarts LFO per voice/note. `sync=false` uses a global free-running LFO age.

### LFO Delay

Used in both JS/FM and OPM/WASM paths:

```js
if delay == 0:
  factor = 1
else:
  waitSeconds = 0.25 * 2 ** (clamp(delay, 0, 99) / 25)
  highDelay = clamp((delay - 75) / 24, 0, 1)
  fadeSeconds = waitSeconds * (1 + 2 * highDelay)
  if age <= waitSeconds:
    factor = 0
  else:
    factor = min(1, (age - waitSeconds) / max(0.001, fadeSeconds))
```

### Mod Wheel Scaling

Mod wheel scales LFO depth:

```js
depthScale = 0.35 + modWheel * 0.65
```

So even with mod wheel at 0, LFO depth is 35% of the patch value.

### LFO Waveforms

LFO output is quantized to 256 steps:

```js
index = floor(frac(phase) * 256) & 255
```

Each waveform returns separate `am` and `pm` values.

Saw Up:

```js
am = (255 - index) / 255
pm = (index < 128 ? index : index - 255) / 128
```

Square:

```js
am = index < 128 ? 1 : 0
pm = index < 128 ? 1 : -1
```

Triangle:

```js
amRaw = index < 128 ? 255 - index * 2 : index * 2 - 256

if index < 64:
  pmRaw = index * 2
else if index < 128:
  pmRaw = 255 - index * 2
else if index < 192:
  pmRaw = 256 - index * 2
else:
  pmRaw = index * 2 - 511

am = amRaw / 255
pm = pmRaw / 128
```

S/Hold:

```js
value = LFO_NOISE_WAVEFORM[index]
am = value / 255
pm = (value - 128) / 128
```

The S/H waveform uses a fixed 256-value YM2151-style noise snapshot, not a runtime random generator.

### Vibrato OSC Behavior

For the JS/FM path:

```js
if round(clamp(PMS, 0, 7)) != 0:
  pitch waveform = selected LFO wave
else:
  pitch waveform = triangle
```

WebDX21 treats PMS=0 with PMD>0 as DX21 "Vibrato OSC" behavior rather than disabling pitch modulation in the JS/FM path.

### Pitch Mod Depth

JS/FM path:

```js
normalized = clamp(PMD, 0, 99) / 99
effectiveSensitivity = PMS == 0 ? 3 : PMS
normalizedSensitivity = clamp(effectiveSensitivity, 0, 7) / 7
pitchDepthSemitones = normalized * normalizedSensitivity * 8
```

Then:

```js
pitchLfo = pitchDepthSemitones * lfoDelayFactor * modWheelDepthScale * pitchWave.pm
```

A one-sample delay is applied:

```js
appliedPitchLfo = previousPitchLfo
previousPitchLfo = pitchLfo
```

### Amp Mod Depth

JS/FM path:

```js
normalized = clamp(AMD, 0, 99) / 99
normalizedSensitivity = clamp(AMS, 0, 3) / 3
ampDepth = normalizedSensitivity * steppedModDepth(normalized)
```

The stepped depth table is:

```text
[0, 0.025, 0.055, 0.11, 0.22, 0.38, 0.58, 0.78, 0.96]
```

Interpolation:

```js
scaled = normalized * (table.length - 1)
index = floor(scaled)
fraction = scaled - index
depth = lower + (upper - lower) * fraction ** 1.35
```

Applied per operator only when `ampModEnable` is true:

```js
ampMod = 1 - ampDepth * lfo.am
```

## OPM/WASM Path

The OPM/WASM path writes DX21 LFO values into YM2151-compatible OPM registers.

### LFO Speed to OPM Byte

Constants:

```js
DX21_LFO_SPEED_MAX = 99
OPM_LFO_FREQ_MAX = 255
LFO_SPEED_35_OPM_BYTE = 0xc0
LFO_SPEED_TO_OPM_EXPONENT =
  log(0xc0 / 255) / log(35 / 99)
```

Mapping:

```js
normalized = clamp(speed, 0, 99) / 99
if normalized <= 0:
  opmByte = 0
else:
  opmByte = round(255 * normalized ** LFO_SPEED_TO_OPM_EXPONENT)
```

This makes DX21 speed 35 approximately OPM byte `0xc0`, and speed 99 `0xff`.

The byte is written to:

```text
OPM register 0x18
```

### PMD / AMD to OPM Registers

Depth scaling combines LFO delay and mod wheel:

```js
depthScale = lfoDelayFactor(delay, lfoDelayAge) * (0.35 + modWheel * 0.65)
```

Then:

```js
pmd = round(pitchDepth * depthScale)
amd = round(ampDepth * depthScale)
```

Mapped to OPM 7-bit depth:

```js
opmAmd = round(amd * 127 / 99)
opmPmd = round(pmd * 127 / 99)
```

Written to OPM register `0x19`:

```text
0x19 = opmAmd
0x19 = 0x80 | opmPmd
```

### Wave to OPM Register

If Vibrato OSC behavior is active, OPM wave is forced to triangle:

```js
wave = lfoUsesVibratoOsc(lfo) ? 2 : clamp(lfo.wave, 0, 3)
```

Written to:

```text
OPM register 0x1b = wave
```

### PMS / AMS to OPM Register

OPM channel register:

```text
0x38 | channel
```

Bits:

```text
bits 4..6: PMS
bits 0..1: AMS
```

Mapping:

```js
pms = lfoUsesVibratoOsc(lfo) ? 3 : clamp(lfo.pitchSensitivity, 0, 7)
ams = clamp(lfo.ampSensitivity, 0, 3)
value = (pms << 4) | ams
```

### Vibrato OSC Detection

```js
lfoUsesVibratoOsc =
  pitchSensitivity == 0 && pitchDepth > 0
```

When true:

- OPM PMS is forced to `3`.
- OPM LFO wave is forced to triangle.
- PMD is still written from pitchDepth.

This is a major difference from the strict Nuked-OPM interpretation where PMS=0 means no PM.

### Initial LFO Depth

For the OPM/WASM path:

```js
initialLfoDepthScale = delay > 0 ? 0 : 1
```

If delay is nonzero, LFO depth starts at zero and is written gradually as the delay envelope rises.

### LFO Delay Update Threshold

OPM/WASM does not update OPM depth registers every sample. It updates when:

```js
abs(nextScale - currentScale) >= 0.015
```

or when forced.

## Implementation Notes for DX21Native

For New engine behavior closer to WebDX21:

1. Keep raw DX21 LFO ranges in patch data.
2. Use WebDX21's Vibrato OSC behavior:
   - if `PMS == 0 && PMD > 0`, use triangle pitch LFO and effective PMS 3.
3. Use WebDX21 OPM/WASM speed mapping for New:
   - speed 35 -> OPM byte `0xc0`
   - speed 99 -> `0xff`
4. PMD/AMD should scale through:
   - LFO delay factor
   - `0.35 + modWheel * 0.65`
5. OPM-style PMD/AMD register values are:
   - `round(depth * 127 / 99)`
6. AMS should be operator-gated by `ampModEnable`.
7. S/H should use the fixed 256-value table, not random noise.

## Difference From Current DX21Native New Engine

Current New engine has already moved PMS toward Nuked-OPM, where PMS=0 disables pitch LFO. WebDX21 instead uses DX21 Vibrato OSC behavior:

```text
PMS=0 and PMD>0 -> triangle pitch LFO with effective PMS 3
```

If WebDX21 is considered the better match for DX21 behavior, DX21Native New should restore this Vibrato OSC behavior for pitch LFO.
