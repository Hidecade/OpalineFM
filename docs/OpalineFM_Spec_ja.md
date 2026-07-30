# Opaline FM 製品仕様

- 文書バージョン：1.0
- 対象製品バージョン：1.0.14
- ブランド：Hidecade Instruments
- ステータス：実装済み仕様

## 1. 製品定義

Opaline FMは、4 Operator FM Synthesis、8 Algorithm、Pitch EG、Operator EG、
LFO、Keyboard Scaling、Performance Mode、Effectsを備えた8音ポリフォニック
Software Synthesizerである。

1980年代のデジタルFM楽器を参考にするが、特定機種またはYM2151 / YM2612 Chipの
完全なEmulationではない。互換Voice Dataを読み書きできる一方、Renderer、
Effects、UI、Library、StateはOpaline FM独自仕様とする。

## 2. 対応Platform

| Platform | 形式 |
|---|---|
| macOS | Standalone、VST3、Audio Unit |
| Windows x64 | Standalone、VST3 |
| iPhone | Standalone App、AUv3 Instrument |

Desktop版はC++17、JUCE、CMakeを使用する。iPhone / AUv3版は共通C++ Engine、
Objective-C++ Bridge、SwiftUI、Apple Audio APIを使用する。

## 3. Synthesis Architecture

```text
MIDI / Screen Keyboard
          │
          ├─ Performance Router ─ Voice A Engine ─┐
          │    SINGLE / DUAL / SPLIT              ├─ Balance ─ Effects ─ Master ─ Stereo Out
          └──────────────── Voice B Engine ───────┘

Each Engine:
  Pitch EG + LFO + Bend + Portamento
                 │
      4 Sine Operators / 8 Algorithms
                 │
       Carrier Sum / Voice Envelope
```

### Polyphony

- Default最大Voice数：8
- Engine内部設定可能範囲：1〜32
- Voice A / BごとにPOLY / MONO
- DUAL時はA / B両EngineをLayer
- SPLIT時はSplit PointでA / BへNoteを振り分け

POLY時は最大Voice数を超えると既存Voiceを整理して新しいNoteへ割り当てる。
MONO時は単一Voiceを保持し、Last NoteとFingered Portamentoを処理する。

## 4. Operator

Operator数：4。全OperatorはSine Oscillatorを基本とし、独立したPhase、
Amplitude Envelope、Frequency Ratio、Detune、Levelを持つ。

### Parameter Range

| Parameter | Range |
|---|---:|
| Attack Rate | 0〜31 |
| Decay 1 Rate | 0〜31 |
| Decay 1 Level | 0〜15 |
| Decay 2 Rate | 0〜31 |
| Release Rate | 0〜15 |
| Ratio Index | 対応Ratio Table |
| Detune | -3〜+3相当 |
| Level | 0〜99 |
| Rate Scaling | 0〜3 |
| Level Scaling | 0〜99 |
| Velocity | 0〜7 |
| AM Enable | Off / On |
| Operator Enable | Off / On |

Parameter読込時は`normalizePatch()`で実装範囲へ制限する。

## 5. Algorithm / Feedback

- Algorithm：1〜8
- Feedback：0〜7
- Feedback対象：Operator 4
- Carrier数：Algorithmにより1〜4

Algorithm Tableは`Source/Engine/OpalineTables.*`を正とする。Operator Dependencyを
明示した固定Tableにより、Modulatorを先に評価しCarrierを合成する。

## 6. Operator Envelope

OperatorごとにAR、D1R、D1L、D2R、RRを保持する。RateはChip系の非線形な時間感を
意識した変換Tableで内部係数へ変換する。

EnvelopeはNote On / Off、Velocity、Rate Scaling、Level Scalingを反映する。
Audio Threadで動的Memory確保を行わず、Sample単位で進行する。

## 7. Pitch

最終Operator周波数は次を組み合わせて決定する。

- MIDI Note
- Patch Transpose
- Operator Ratio
- Operator Detune
- Pitch Envelope
- LFO Pitch Modulation
- Mod Wheel Pitch Modulation
- Pitch Bend
- DUAL Detune
- Portamento

### Pitch Bend

- Normalized入力：-1〜+1
- Bend Range：0〜12 semitones

### Portamento

- Value：0〜99
- Mode：Off、Full、Finger
- CC65はPOLY / Full Portamento Switchとして使用

## 8. Pitch Envelope

- PR1、PR2、PR3：0〜99
- PL1、PL2、PL3：0〜99
- Neutral Level：50

3区間のPitch変化を生成する。高Rateでは短時間で目標Levelへ移動し、
LevelはSemitone Offsetへ非線形変換する。

## 9. LFO

Wave：

1. Saw Up
2. Square
3. Triangle
4. Sample & Hold

Parameter：

| Parameter | Range |
|---|---:|
| Speed | 0〜99 |
| Delay | 0〜99 |
| PMD | 0〜99 |
| AMD | 0〜99 |
| PMS | 0〜7 |
| AMS | 0〜3 |
| Sync | Off / On |

Global LFOをEngine単位で生成する。Pitch ModulationとAmplitude Modulationへ
分配し、Operator AM Enableを反映する。Sync OnではNote On時にLFO AgeをResetする。

## 10. Keyboard Scaling / Velocity

### Rate Scaling

MIDI Noteに応じてOperator Envelope Rateを増加させる。

### Level Scaling

MIDI Note 60付近を基準としてOperator Levelを補正する。

### Velocity

MIDI VelocityとOperator Velocity Sensitivityを組み合わせ、Carrier音量と
Modulation量へ反映する。

## 11. Render Model

`OpalineRenderModel`は次の2 Modeを持つ。

- `Type A`：安定したSnapshot Renderer
- `Type B`：現行の編集可能Comparison Renderer

StateにRender Modelを保存する。UI表示とEngine設定を同期し、Voice切替や
Host State復元後も同じModelを使用する。

## 12. Performance

### SINGLE

Voice A Engineだけを出力する。

### DUAL

Voice A / Bを同時に発音し、BへDetuneを加える。A/B Balanceを出力Gainへ反映する。

### SPLIT

Split Pointを境界にMIDI NoteをVoice A / Bへ振り分ける。

Performance State：

- Mode
- Voice A / B Index
- Mono A / B
- Portamento Mode A / B
- Dual Detune
- Split Point
- A/B Balance

## 13. Effects

Engine出力後にStereo Effect Chainを処理する。

### Parameter

| Parameter | Range |
|---|---:|
| Reverb（Character＋Wet） | 0〜99 |
| Delay（Time / Feedback＋Wet） | 0〜99 |
| Tone | 0〜99 |
| Chorus | 0〜99 |
| Spread | 0〜99 |
| Pan | 0〜99（50＝Center） |
| Effects Enabled | Off / On |

### Processing

- Multi-tap Feedback Reverb
- Stereo Delay
- Modulated Chorus Delay
- Tone Low-pass
- Dry / Wet Gain Compensation
- Effect Parameterがすべて0の場合のDry-only最適化

Bufferは`prepare()`時にSample Rateに応じて確保する。最大Delay長は0.8 seconds、
最大Chorus Buffer長は0.04 secondsとする。

## 14. Output / Declick

- Stereo Output
- Master Volume
- Voice終了時の不要なClickを抑制
- 出力Headroomを確保
- NaN / Infを出力しない
- Scope用SnapshotをRealtime-safeに公開

Waveform Scopeは最後に押したNoteのEffect前Voice信号からDCを除去し、
Note周期に近いWindowへ整列して256 Sample表示を生成する。表示用Auto Gainを
適用し、最終Stereo OutputおよびLevel Meterとは分離する。

## 15. Voice Library

- Bank数：8
- 1 Bank：32 Voice
- 合計：256 Slot
- Factory Library：`assets/factory.opalinelibrary.xml`
- Compatible Factory Bank：`assets/factory.syx`

LibraryはDesktopとiPhoneで共通の論理構造を使用する。

操作：

- Compatible SysEx Bank Load / Save
- Full Library XML Export / Import
- Single Voice Load / Save
- Copy / Paste
- Init
- Store

## 16. Compatible SysEx

対応Bulk Voice仕様：

- Voice数：32
- VMEM Voice Size：128 bytes
- VCED Voice Size：93 bytes
- Yamaha Checksum検証 / 生成

Decode / Encode対象：

- Operator EG
- Ratio / Detune / Level
- Algorithm / Feedback
- LFO
- Pitch EG
- Transpose
- Name

Opaline独自Effects、Performance、Render Modelなど、互換Formatに存在しない項目は
SysExへ保存しない。

## 17. Chip Voice Import

対応形式：

- OPM：YM2151 VOPM Text
- TFI：YM2612、42-byte
- VGI：YM2612、43-byte
- DMP：DefleMask Version 10 / 11のYM2151 / YM2612 FM Data

ImporterはOperator OrderをOpaline順へ変換し、Algorithm、Feedback、Multiplier、
Detune、Total Level、Envelope、Rate Scaling、AM、利用可能なLFO情報をMappingする。

YM2151 DT2はMultiplierと合成し、最も近いOpaline Ratio Indexへ量子化する。
未対応SSG-EGはWarningを返して無視する。Sourceに存在しないParameterはDefaultへ
初期化し、直前Patchの値を引き継がない。

## 18. File Format

| Format | 用途 |
|---|---|
| `.syx` | Compatible 32 Voice Bank |
| `.opalinevoice` | Opaline Single Voice |
| `.opalinelibrary.xml` | 8 Bank Library |
| `.opalinefmstate` | Full Application / Plug-in State |
| `.opm` | YM2151 Voice Source |
| `.tfi` | YM2612 Voice Source |
| `.vgi` | YM2612 Voice Source |
| `.dmp` | DefleMask FM Voice Source |
| `.wav` | Standalone Stereo Recording |

読込値はNormalizeし、不正Length、Checksum、Unsupported Version、範囲外値を
拒否または安全な値へ補正する。

## 19. MIDI

| Message | 動作 |
|---|---|
| Note On / Off | Voice発音 / Release |
| Velocity | Operator Velocity処理 |
| Pitch Bend | Bend Rangeに従うPitch変化 |
| CC 1 | Mod Wheel |
| CC 64 | Sustain Pedal |
| CC 65 | Portamento Switch |
| All Notes Off / Panic | VoiceとPedal StateをReset |

Standaloneは選択されたMIDI Inputを使用する。Plug-in / AUv3はHostからの
MIDI Eventを処理する。

## 20. WAV Recorder

Desktop Standaloneは最終Stereo出力をRealtime Ring Bufferへ記録する。

- Audio Threadは固定Bufferへ書き込む
- Collector Threadが記録Dataを集約
- Stop後にFile I/Oを実行
- 空Recordingは保存しない
- Overflow Frame数を追跡

## 21. State

Full Stateには次を保存する。

- Patch
- Performance State
- Master Volume
- Pitch Bend Range
- Portamento
- Mod Wheel Range
- Effects Enabled
- Render Model
- Voice Library / Bank / Voice選択

Plug-in HostのState保存 / 復元とStandalone設定復元で同じ論理Stateを使用する。

## 22. UI

### Desktop

- Header：Audio / MIDI Status、WAV、Library、Effect
- Voice A / B選択
- SINGLE / DUAL / SPLIT
- Operator 1〜4
- Algorithm / Feedback
- Pitch EG / LFO
- Effects
- Pitch / Mod Wheel
- Screen Keyboard
- Trigger-aligned Waveform Monitor

### iPhone

- Landscape専用
- Play / Edit画面
- Screen Keyboard
- Section分割されたParameter Editor
- Standalone Audio / Core MIDI
- AUv3 Extension UI

Platform間でEngine、Parameter意味、Voice Dataを共有し、Layoutと入力方式を
個別最適化する。

## 23. Realtime要件

- Render中にFile I/Oを行わない
- UI操作をAudio Threadから呼ばない
- Realtime Command QueueでEvent / Stateを渡す
- Effect Bufferを`prepare()`時に確保する
- 出力をFiniteに保つ
- Sample Rateと可変Block Sizeへ対応する
- Host Offline Renderでも共通Engineを使用する

## 24. 実装基準

仕様とCodeが不一致の場合、v1.0.14の実装を基準とする。

- Patch / Range：`Source/Engine/OpalineTypes.*`
- FM Voice：`Source/Engine/OpalineVoice.*`
- Voice / Effects：`Source/Engine/OpalineEngine.*`
- Algorithm：`Source/Engine/OpalineTables.*`
- Envelope：`Source/Engine/OpalineEnvelope.*`
- Pitch EG：`Source/Engine/OpalinePitchEnvelope.*`
- SysEx：`Source/Engine/OpalineSysex.*`
- Chip Import：`Source/Engine/ChipVoiceImport.*`
- Library：`Source/Engine/OpalineVoiceLibrary.*`
- Desktop UI：`Source/App/MainComponent.*`
- Plug-in：`Source/Plugin/*`
- iPhone / AUv3：`iOS/OpalineFMMobile/`

ユーザー操作は[`OpalineFM_Manual_ja.md`](OpalineFM_Manual_ja.md)を参照する。
