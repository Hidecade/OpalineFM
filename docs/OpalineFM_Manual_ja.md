# Opaline FM 取扱説明書

- 対象バージョン：1.0.14
- ブランド：Hidecade Instruments
- 対応環境：macOS、Windows、iPhone / AUv3

## 1. はじめに

Opaline FMは、4つのOperatorと8種類のAlgorithmを備えた、8音ポリフォニック
FMシンセサイザーです。1980年代のデジタルFM楽器を参考にしていますが、
特定機種や音源Chipの完全なEmulationではありません。

単体アプリ、DAW用Instrument、iPhone App / AUv3として使用できます。
互換32音色SysEx BankのLoad / Saveと、YM2151 / YM2612系の単音色Importに
対応します。

## 2. 対応形式

| Platform | 形式 |
|---|---|
| macOS | Standalone、VST3、Audio Unit |
| Windows | Standalone、VST3 |
| iPhone | Standalone App、AUv3 Instrument |

## 3. インストール

### macOS

目的に合う署名・Apple公証済みPackageを実行します。

- `OpalineFM-Standalone-1.0.14-macOS.pkg`
- `OpalineFM-VST3-1.0.14-macOS.pkg`
- `OpalineFM-AU-1.0.14-macOS.pkg`

標準のInstall先：

```text
Standalone: /Applications/
VST3:       /Library/Audio/Plug-Ins/VST3/
Audio Unit: /Library/Audio/Plug-Ins/Components/
```

Plug-inをInstallした後はDAWを再起動するか、Plug-inを再Scanしてください。

Logic Proでは、Software Instrument Trackの`Instrument`Slotから
`AU Instruments > Hidecade > Opaline FM > Stereo`を選択します。

### Windows

目的に合う64-bit Installerを実行します。

- `OpalineFM-Standalone-v1.0.14-Windows-x64.exe`
- `OpalineFM-VST3-v1.0.14-Windows-x64.exe`

VST3は通常、次の場所へInstallされます。

```text
C:\Program Files\Common Files\VST3\Opaline FM.vst3
```

Windows版Installerは未署名です。公式GitHub Releaseから取得したFileであることを
確認してから、Windowsの警告を許可してください。

### iPhone

App StoreからInstallします。Standalone Appとして画面Keyboardや外部MIDIで
演奏できるほか、GarageBandなどのAUv3対応HostからInstrumentとして使用できます。

## 4. Quick Start

1. Opaline FMを起動します。
2. Standalone版では`Options`からAudio出力とMIDI入力を設定します。
3. BankとVoice Aを選びます。
4. MIDI Keyboardまたは画面Keyboardを演奏します。
5. `ALG`、Operatorの`Level`、Envelopeを変更して音を作ります。
6. 編集を残す場合は`STORE`を押します。

Plug-in版ではAudio / MIDIをHostが管理します。VST3ではDAWのShortcutを妨げない
よう、PCの文字Keyによる発音を無効にしています。

画面KeyboardではC音の下部に`C2`、`C3`、`C4`のようなOctave名を表示します。
Desktop版の左端はC2、iPhone版の初期位置はC3です。

Standalone版のPC KeyboardはAurelineと共通です。下段は`Z`=C2から始まり、
上段は`Q`=F3から始まります。上段の黒Keyは`2`=F#3、`3`=G#3、
`4`=A#3、`6`=C#4、`7`=D#4の順です。日本語配列では`_`=F3、
`]`=F#3、`¥`=C#5として使用できます。

## 5. Header

- Product名とVersion
- `Audio`：現在の出力Device。Plug-inでは`host`
- `MIDI`：現在の入力。Plug-inでは`host`
- `WAV`：Standalone版の録音
- Library操作
- Effect全体On / Off

Device名が長い場合も、Audio / MIDI表示はButtonへ重ならない範囲で表示します。

## 6. Voice Library

Opaline FMは8 Bank、各32 Voice、合計256 Slotの書換可能Libraryを持ちます。

### Bank操作

- `LOAD`：互換`.syx` BankまたはOpaline Library XMLを読み込む
- `SAVE`：現在の32 Voice Bankを`.syx`として保存
- `EXPORT`：8 Bank全体を`.opalinelibrary.xml`として保存

互換SysEx BankのLoadは、現在選択中のBank 32 Slotを置き換えます。必要なBankは
先に`SAVE`または`EXPORT`でBackupしてください。

### 単音色操作

SINGLE Modeでは次を使用できます。

- `LOAD`：Opaline Voiceまたは対応Chip Voiceを編集Bufferへ読み込む
- `SAVE`：現在の単音色を`.opalinevoice`へ保存
- `COPY` / `PASTE`：Voice名を含めて編集Buffer内で複製
- `INIT`：Voiceを初期化
- `STORE`：現在のVoiceとVoice名を選択Slotへ確定保存

`LOAD`、`PASTE`、`INIT`は編集Bufferへの一時適用です。`STORE`せずに別Voiceを
選ぶと編集内容は失われます。

## 7. Performance Mode

### SINGLE

Voice Aだけを使用します。単音色の編集、Load / Save、Copy / Paste、Init、
Storeを使用できます。

### DUAL

Voice AとVoice BをLayerします。

- `DETUNE`：Voice BのPitch Offset
- `BALANCE`：A / Bの音量Balance

### SPLIT

Keyboardの低音側と高音側へVoice A / Bを割り当てます。

- `SPLIT`：境界Note
- `BALANCE`：A / Bの音量Balance

Voice A / Bは個別に`POLY`または`MONO`を設定できます。

## 8. FM Synthesisの基本

Opaline FMは4つのSine Operatorを使用します。

- Carrier：最終出力へ直接加算されるOperator
- Modulator：別Operatorの周波数を変調し、倍音を作るOperator

Operator同士の接続は`ALG`で決まります。同じOperator設定でもAlgorithmを変えると、
CarrierとModulatorの役割が変わり、音量や音色が大きく変化します。

### ALG

8種類の4 Operator接続から選択します。Algorithm 1は深い直列変調、
Algorithm 8は4 OperatorすべてがCarrierとなる構成です。

### FB

Operator 4のFeedback量を0〜7で設定します。上げるほど倍音が増え、
SawやNoiseに近い硬い音へ変化します。

## 9. Operator

各Operatorは次のParameterを持ちます。

| Parameter | 意味 |
|---|---|
| `AR` | Attack Rate |
| `D1R` | Decay 1 Rate |
| `D1L` | Decay 1 Level |
| `D2R` | Decay 2 Rate |
| `RR` | Release Rate |
| `RATIO` | 基準Pitchに対する周波数比 |
| `DETUNE` | 微小Pitch Offset |
| `LEVEL` | Carrier音量またはModulation Depth |
| `RATE SC` | Keyboard位置によるEnvelope Rate変化 |
| `LEVEL SC` | Keyboard位置によるLevel変化 |
| `VEL` | Velocity感度 |
| `AM` | LFO Amplitude Modulation |
| Operator On / Off | Operatorの有効化 |

Carrierの`LEVEL`は主に音量を、Modulatorの`LEVEL`は倍音量と明るさを変えます。
音が出ない場合は、現在のAlgorithmでCarrierになっているOperatorがOnで、
Levelが十分にあるか確認してください。

## 10. Amplitude Envelope

各Operatorは独立した多段Envelopeを持ちます。

```text
Note On:
  ARで最大Levelへ上昇
  D1RでD1Lへ下降
  D2Rで継続的に下降

Note Off:
  RRで0へ下降
```

Rate値は時間そのものではなく変化速度です。基本的に大きいRateほど速く変化します。
CarrierとModulatorで異なるEnvelopeを設定すると、音量と倍音の時間変化を
独立して作れます。

## 11. Pitch Envelope

- `PR1`、`PR2`、`PR3`：各区間のPitch変化速度
- `PL1`、`PL2`、`PL3`：各区間のPitch Level

中央値付近が基準Pitchです。BrassのAttack、Percussion、Kick、Laser、
特殊効果などのPitch変化に使用します。

## 12. LFO

Wave：

- Saw Up
- Square
- Triangle
- Sample & Hold

Parameter：

- `SPEED`：LFO速度
- `DELAY`：Note On後に直接LFOが作用するまでの時間
- `PMD`：Pitch Modulation Depth
- `PMS`：Pitch Modulation Sensitivity
- `AMD`：Amplitude Modulation Depth
- `AMS`：Amplitude Modulation Sensitivity
- `SYNC`：Note On時にLFO位相をRestart

Amplitude Modulationは、Operator側の`AM`がOnの場合に作用します。

## 13. Modulation Wheel

Mod Wheelは音色に保存された直接LFO量とは別の演奏用Modulation Sourceです。

- `MOD PITCH`：WheelによるPitch Modulation Range
- `MOD AMP`：WheelによるAmplitude Modulation Range

Voice Aを選択した時点ではPMD / AMDを基準に初期化され、その後は演奏設定として
独立して変更できます。

## 14. Pitch Bend / Portamento / Pedal

- `RANGE`：Pitch Bend幅、0〜12 semitones
- `PORTA`：Portamento時間、0〜99
- `OFF`：Portamentoなし
- `FULL`：前のNoteから常にGlide
- `FINGER`：Keyを重ねて弾いた時だけGlide

POLYではOFF / FULL、MONOではOFF / FULL / FINGERを使用できます。

標準MIDI操作：

| MIDI | 動作 |
|---|---|
| Pitch Bend | `RANGE`に従うPitch変化 |
| CC 1 | Modulation Wheel |
| CC 64 | Sustain Pedal |
| CC 65 | POLY / FULL Portamento Switch |

## 15. Effects

Opaline FMは最終出力に次のEffectを持ちます。

- `REVERB`：Reverbの性格とWet量
- `DELAY`：Delay時間・FeedbackとWet量
- `CHORUS`：Chorus量
- `SPREAD`：発音中のVoiceを左右へ分散
- `PAN`：Effectを含む最終出力の左右位置
- `TONE`：Effectを含む出力Tone
- `EFFECT`：Effect Chain全体On / Off

Effect Parameterがすべて0の場合はDry経路を使用します。Import元にEffect情報が
ない場合は、意図しない引継ぎを避けるためEffectを初期化します。

## 16. Waveform Monitor

画面上部のMonitorは、最後に押したNoteのFM Voice波形を表示します。

- Note OnをTriggerとして表示区間を整列
- DC Offsetを除去
- 押されたNoteの周期に近いWindowを選択
- 視認性のためAuto Gainを適用

Algorithm、Feedback、Envelope、Modulationによる変化を確認するための
Sound Design表示です。Effect後の最終波形や、校正された音量Meterではありません。

## 17. YM2151 / YM2612 Voice Import

SINGLE Modeの単音色`LOAD`から、次の形式を読み込めます。

| 拡張子 | 形式 | 動作 |
|---|---|---|
| `.opm` | YM2151 VOPM Text | Voice一覧から1 Voiceを選択 |
| `.tfi` | YM2612 TFM Music Maker | 42-byte Voiceを読込 |
| `.vgi` | YM2612 VGM Music Maker | 43-byte Voiceを読込 |
| `.dmp` | DefleMask FM Preset | 対応Version 10 / 11を読込 |

ImporterはOperator順、Algorithm、Feedback、Multiplier、Detune、Total Level、
Envelope Rate / Level、Rate Scaling、AM、利用可能なLFO情報をOpaline形式へ
変換します。

YM2151のDT2はMultiplierと合成し、最も近いOpaline Ratioへ割り当てます。
未対応のSSG-EGは無視します。元形式にないParameterは直前Voiceから継承せず、
Pitch EGをNeutral、EffectsをOffなど安全なDefaultへ初期化します。

Opaline FMはYM2151 / YM2612 Chip Emulatorではないため、変換後の発音は
近似であり、元Chipと完全一致するとは限りません。

iPhone版でOPMを読み込む場合は、現在はFile内の先頭Voiceを読み込みます。

## 18. Compatible SysEx

`.syx`は32 Voice BankのLoad / Saveに使用します。

- Bulk Bank：32 Voice
- 対応Voice DataをOpaline Patchへ変換
- Save時は互換VMEM VoiceへEncode
- Opaline独自Effectsなど、互換SysExに存在しない情報は保存されない

Opaline固有設定をすべて保持したい場合は、`.opalinevoice`または
`.opalinelibrary.xml`を使用してください。

## 19. WAV Recording

macOS / Windows Standalone版で使用できます。

1. `WAV`を押して録音を開始します。
2. Button表示が`STOP`へ変わります。
3. 演奏します。
4. `STOP`を押します。
5. 保存先とFile名を選択します。

最終Stereo出力を現在のSample RateでWAV保存します。録音中に音声が出力されて
いない場合は、空Fileを作成せずMessageを表示します。

Plug-in版ではDAWの録音 / Bounce機能を使用してください。

## 20. iPhone / AUv3

iPhone版はLandscape専用UIを使用します。Play画面とEdit画面を切り替えながら、
画面Keyboardを使って演奏できます。Keyboardの初期位置はC3で、C音にはOctave名を
表示します。

- Standalone Audio
- Core MIDI入力
- AUv3 Instrument
- Voice / Bank Library
- Operator / Envelope / LFO / Effect編集
- SINGLE / DUAL / SPLIT
- File Import / Export
- Host State保存 / 復元

AUv3ではAudioとMIDIをHostが管理します。

## 21. File Format

| 拡張子 | 用途 |
|---|---|
| `.syx` | 互換32 Voice SysEx Bank |
| `.opalinevoice` | Opaline単音色 |
| `.opalinelibrary.xml` | 8 Bank Library |
| `.opalinefmstate` | Standalone / Plug-inの完全State |
| `.opm` | YM2151 VOPM Voice |
| `.tfi` | YM2612 TFM Voice |
| `.vgi` | YM2612 VGM Voice |
| `.dmp` | DefleMask YM2151 / YM2612 FM Preset |
| `.wav` | Standalone録音 |

## 22. Troubleshooting

### 音が出ない

- StandaloneのAudio出力を確認する
- MIDI入力または画面Keyboardを確認する
- Master Volumeを上げる
- Carrier OperatorがOnか確認する
- Carrier LevelとEnvelopeを確認する
- Effectを一度Offにする
- Sustainが残っている場合はPanic / All Notes Offを送る

### 音色編集が消えた

別Voiceを選ぶ前に`STORE`してください。外部Backupには単音色`SAVE`または
Library `EXPORT`を使用します。

### SysExを保存したらEffectが消えた

互換SysExに存在しないOpaline独自Parameterは保存されません。完全保存には
`.opalinevoice`または`.opalinelibrary.xml`を使用してください。

### Plug-inがDAWに表示されない

- DAWを再起動する
- Plug-inを再Scanする
- macOSではAU / VST3、WindowsではVST3を正しい場所へInstallする
- Logic ProではInstrument Slotを使用する

### CPU負荷や音切れがある

- Audio Buffer Sizeを少し大きくする
- DUAL ModeからSINGLEへ切り替えて比較する
- Reverb / Delay / Chorusを一時的に下げる
- 他のApplicationやPlug-inの負荷を確認する

## 23. 関連情報

- [Opaline FM GitHub](https://github.com/Hidecade/OpalineFM)
- [最新Release](https://github.com/Hidecade/OpalineFM/releases/latest)
- [製品仕様書](OpalineFM_Spec_ja.md)
- [iPhone / AUv3実装情報](../iOS/OpalineFMMobile/README.md)
