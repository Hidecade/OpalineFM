# Opaline FM 仕様書

Opaline FMは、C++/JUCEで実装された4オペレーターFMシンセサイザーです。1980年代のクラシックな4オペレーター・デジタルFM楽器を参考にしつつ、実用的な互換音色バンクの取り扱いに重点を置いています。FM音源方式を採用していますが、チップエミュレーションではなく、実際のFM音源機器を完全再現するものではありません。

詳細な調査記録は、リポジトリ外の非公開資料フォルダーで管理しています。この文書は、開発、リリースノート、公開文書向けの概要です。

インストール方法と操作方法は、[日本語README](../README_ja.md)または[英語README](../README.md)を参照してください。本書は開発者・保守担当者向けに、音源動作、パラメーターの所有範囲、ファイル互換性、実装上の制約を定義します。

## 目標

- スタンドアロンアプリ、VST3インストゥルメント、プラグイン版スタンドアロン、macOS AUのビルド経路を提供する。
- 互換32音色SysExバンクを読み書きする。
- 音楽用シンセサイザーとしても、実測した4オペレーターFM動作の参照実装としても利用できるエンジンにする。
- 公開名称をOpaline FMとし、ファイル形式やパラメーター互換性を示す場合に限り、内部で互換用語を維持する。

## 製品名

- 公開製品名: `Opaline FM`
- CMakeプロジェクト: `OpalineFM`
- メインプラグインターゲット: `OpalineFM_Plugin`
- スタンドアロンアプリターゲット: `OpalineFM_Standalone`
- プラグイン状態ファイル拡張子: `.opalinefmstate`
- 音色ライブラリーの既定エクスポート名: `OpalineFM_Voice_Library.opalinelibrary.xml`

## ビルド出力

Windowsデバッグビルドの出力先:

```text
build/standalone-vs-debug/OpalineFM_Plugin_artefacts/Debug/VST3/Opaline FM.vst3
build/standalone-vs-debug/OpalineFM_Plugin_artefacts/Debug/Standalone/Opaline FM.exe
build/standalone-vs-debug/OpalineFM_Standalone_artefacts/Debug/Opaline FM.exe
```

macOS AUビルドターゲット:

```bash
cmake --preset macos-debug
cmake --build --preset plugin-au-macos-debug
```

AUの想定出力先:

```text
build/macos-debug/OpalineFM_Plugin_artefacts/Debug/AU/Opaline FM.component
```

## エンジン概要

オーディオエンジンは`Source/Engine`以下にあり、すべてのフロントエンドで共有されます。

主な構成:

- `OpalinePatch`は、互換範囲の音色パラメーターを保持する。
- `OpalineEngine`は、ボイス、MIDIノート状態、ピッチベンド、モジュレーションホイール、サンプル生成を管理する。
- `OpalineVoice`は、オペレーター接続、エンベロープ、LFO、ピッチエンベロープ、フィードバック、出力量子化およびモデル差を処理する。
- 公開レンダリング経路は、オペレーターのレベル処理、アッテネーション、フィードバック、キャリアミックス、出力挙動を扱う。

内部名の`opaline`は、互換レベルのデータ構造とSysExの意味を表すため維持しています。

### リアルタイム安全性

- デスクトップ版とiOS版の音声レンダーは、UI・ライブラリー状態を保護するmutexを取得しない。
- Note On/Off、Pitch Bend、Mod Wheel、Sustain、Portamento Foot Switch、Panicは、固定長の`RealtimeCommandQueue`で音声スレッドへ渡す。
- 音色、エフェクト、演奏モードなどのまとまった状態は、固定スロットの`RealtimeStateMailbox`でバッファ境界に反映する。
- キューとメールボックスの消費者は音声レンダーだけとし、UIスレッドがリアルタイムコマンドを実行しない。
- WAV録音では、音声スレッドは事前確保したリングバッファへ書き込み、録音データの拡張・集約とファイル保存は別スレッドまたはUI側で行う。
- デスクトップ版の音声コールバックは、デバイス照会や文字列比較を行わず、開始時に決定した出力補正値をatomicで参照する。

### iOSオーディオライフサイクル

iOSスタンドアロン版は、出力ルート変更、`AVAudioSession.interruptionNotification`、`mediaServicesWereResetNotification`、およびSwiftUIの`scenePhase`を監視する。割り込みやバックグラウンド移行時に音声を停止し、復帰時にはセッションのサンプルレートとルートを再取得して`AVAudioEngine`を再構成する。電話、Siri、Bluetooth、イヤホン変更後の最終確認は実機回帰テストで行う。

## 4オペレーターFMアーキテクチャ

各ボイスは4つのサイン波オペレーターを持ちます。各オペレーターは`baseFrequency * ratio + detune`の周波数で信号を生成します。アルゴリズムテーブルによって、その出力を最終キャリアミキサーへ送るか、別のオペレーターの位相変調へ使うかが決まります。

- **キャリア**は可聴出力へ直接加算される。
- **モジュレーター**は後段オペレーターの位相を変え、倍音構成を変化させる。
- オペレーターの役割に応じて、LEVELはキャリア振幅またはモジュレーション指数を制御する。
- オペレーター4がフィードバックループを持ち、FBで量を設定する。
- アルゴリズム1～4は主に直列変調、5～8は並列分岐と複数キャリアを使用する。
- 並列キャリアはボイス出力処理前に加算する。後述の軽いキャリア数補正を適用する。

オペレーターごとの信号順序:

```text
note + transpose + bend + PEG + pitch LFO + portamento
    -> ratio/detune oscillator
    -> phase modulation and OP4 feedback
    -> amplitude EG
    -> LEVEL / LevelSc / velocity
    -> AM（有効時）
    -> carrier output または downstream modulation bus
```

各ボイスをミックスしてリミット/デクリック処理を行い、その後コーラス、ディレイ、リバーブ、Wet Mix、Tone処理へ送ります。

### アルゴリズム接続表

内部のオペレーター番号は`0..3`ですが、仕様上はユーザー表示に合わせて`OP1..OP4`で表記します。`>`は右側のオペレーターを位相変調することを表し、`+`は並列の加算を表します。

| ALG | 接続 | キャリア |
| --- | --- | --- |
| 1 | `OP4 > OP3 > OP2 > OP1` | OP1 |
| 2 | `(OP4 + OP3) > OP2 > OP1` | OP1 |
| 3 | `OP3 > OP2 > OP1` と `OP4 > OP1` | OP1 |
| 4 | `OP2 > OP1` と `OP4 > OP3 > OP1` | OP1 |
| 5 | `OP2 > OP1` と `OP4 > OP3` | OP1、OP3 |
| 6 | `OP4 > (OP1 + OP2 + OP3)` | OP1、OP2、OP3 |
| 7 | `OP4 > OP3`、`OP1`、`OP2` | OP1、OP2、OP3 |
| 8 | `OP1 + OP2 + OP3 + OP4` | OP1、OP2、OP3、OP4 |

### 位相変調計算

レンダラーは、オペレーター出力を音声用の`audio`と、下流オペレーターの位相変調へ渡す`modulation bus`に分けて扱います。`modulation bus`は整数バスとして扱い、1サンプルごとに依存関係の深いモジュレーターから順に計算します。

主な定数:

```text
phaseSteps = 1048576
sineIndexSteps = 1024
operatorBusPeak = 8192
tlSubsteps = 8
tlMax = 127
logAttenuationDbPerStep = 6.020599913279624 / 256
```

各オペレーターの周波数と位相進行:

```text
ratio = ratioTable[ratioIndex]
frequency = baseFrequency * ratio + dt1Offset(baseFrequency, ratio, detune, midiNote)
phaseIncrement = round(clamp(frequency, 0, sampleRate * 0.49) * phaseSteps / sampleRate)
phase += clamp(phaseIncrement, 0, phaseSteps - 1) * 2*pi / phaseSteps
basePhaseIndex = floor(fract(phase / (2*pi)) * sineIndexSteps) & 1023
```

下流オペレーターへ入る位相変調バスは、接続元オペレーターの`modulation bus`を加算してから、算術右シフト相当で1/2にします。負数は整数丸めを一定にするため、`-((abs(x) + 1) / 2)`で丸めます。

```text
rawPmBus = sum(upstreamOperator.modulationBus)
pmBus = round(arithmeticShiftRightOne(rawPmBus))
pmBus = clamp(pmBus, -8192, 8191)
pmIndex = round(pmBus)
```

OP4だけはフィードバックを持ちます。直前2サンプルのOP4 `modulation bus`を加算し、同じく算術右シフト相当で1/2にしてからFB量を位相インデックスへ変換します。

```text
feedbackBus = arithmeticShiftRightOne(op4History[0] + op4History[1])

if FB == 0:
    feedbackIndex = 0
else:
    feedbackIndex = round(clamp(round(feedbackBus), -8192, 8191) / 2 ^ (9 - FB))
```

最終的なサインテーブル位置は1024ステップでラップします。

```text
phaseIndex = (basePhaseIndex + pmIndex + feedbackIndex) & 1023
```

レンダラーは、サイン波を直接`sin()`で計算するのではなく、1024ステップの位相インデックスから符号とlog-sine減衰量を求め、エンベロープ、TL、ベロシティ、AMを加えた総減衰量を指数テーブルで音声値へ戻します。

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

`targetTl`は急に飛ばさず、約`0.012 s`のランプで`currentTl`へ追従します。これにより、LEVELやLevelScの変化でクリックが出にくくなります。

## パラメーターの所有範囲

音色パラメーターは`OpalinePatch`へ保存され、元形式が対応する範囲で互換音色データとともに移動します。対象はオペレーター設定、アルゴリズム、フィードバック、Pitch EG、LFO、トランスポーズ、エフェクトです。

次のパフォーマンスパラメーターはSysEx音色ではなく、アプリケーション/プラグイン状態へ保存します。

- SINGLE/DUAL/SPLITモードとA/B音色選択
- A/BそれぞれのPOLY/MONO
- DUAL Detune、SPLITポイント、A/B Balance
- Pitch Bend RANGEとPORTA
- MOD PITCHとMOD AMPのホイールレンジ
- Master Volumeとレンダリングモデル

音色Aを選択すると、そのPMD/AMDでMOD PITCH/MOD AMPを初期化します。その後のホイールレンジ編集値は、別のA音色を選ぶまでパフォーマンス状態として維持します。

## UIとホスト

フロントエンドには2つの経路があります。

- フルスタンドアロンアプリ: オーディオデバイスとMIDI入力の選択を管理する。
- プラグインエディター: `MainComponent`をホストモードで再利用し、DAW/プラグインラッパーがオーディオとMIDI経路を管理する。

プラグインエディターがプロセッサーから受け取る状態:

- MIDIノート表示状態
- ピッチベンドとモジュレーションホイールの状態
- ホストが対応している場合の選択プログラム名

## 音色バンク

Opaline FMは、互換32音色バルクSysExバンクに対応します。

ユーザー向け対応ファイル形式:

- `.syx`: 互換32音色バンク1個
- `.opalinelibrary.xml`: Opaline形式の複数バンク・ライブラリー
- `.opalinefmstate`: プラグイン版スタンドアロンの完全な状態ファイル

再配布条件を確認できない著作権保護対象のファクトリーバンクは、公開リリースへ同梱しません。ユーザーは自身の`.syx`ファイルを読み込めます。

## ピッチエンベロープジェネレーター

Pitch EGは、独立した高レベルのピッチエンベロープとして実装しています。

- PLの中央は`50`。
- PL範囲は約`-4800`から`+4800`セント。
- PRは移動速度を制御する。
- PR1はキーオン後、PL3からPL1へ移動する。
- PR2はPL1からPL2へ移動する。
- PL2は押鍵中に保持されるピッチレベル。
- PR3はキーオフ後、PL3へ移動する。

現在の実測PR基準値:

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
PR 99 : 短い可聴遷移、約12 ms
```

PR90～98付近の高速値には実測補正データを使用します。PR99は数学的な完全なゼロ時間ではなく、弦や撥弦系のピッチアタックに小さな「プッ」という過渡音を残すため、短い可聴遷移として実装しています。

## キーボード・レベルスケーリング

Keyboard Level Scaling（`LevelSc`）は、DX21のキャリアおよびモジュレーターを個別に録音した実測値に基づきます。両方のオペレーター役割で同じスケーリング値になるため、共通の実測テーブルを使用します。ホストによってC0/C1などのオクターブ表記が異なるため、仕様ではMIDIノート番号を基準とします。

実装は単一の近似式ではなく、下表をルックアップアンカーとして使用します。表の間は1オクターブごとに約2倍となる曲線で補間し、`LevelSc`の中間値は隣接する測定行の間を線形補間します。最後に最も近い整数へ量子化し、`0..127`に制限します。ノート36未満および96より上は同じオクターブ比で外挿します。

`LevelSc`とRate Scalingは、受信したMIDIノートにTransposeを加えた発音キーを基準にします。例えばMIDIノート60をTranspose -12で鳴らした場合、発音音程、`LevelSc`、Rate Scalingのすべてにノート48相当の値が適用されます。そのため「ノート60／Transpose -12」と「ノート48／Transpose 0」は同じ音質になります。これは、Transposeを変更しても音色が変化しないDX21実機の挙動に合わせたものです。

Detuneの周波数テーブルも、同じTranspose後の発音キーを参照します。

MIDIノート36、48、60、72、84、96での実測TL相当減衰量:

```text
LevelSc  36  48  60  72  84  96
      0   0   0   0   0   0   0
     25   0   1   2   4   8  16
     50   1   2   4   8  16  33
     75   1   3   6  12  25  50
     99   1   3   7  16  33  67
```

## キーボード・レートスケーリングの実測

DX21のRate Scaling実測には、次の4音色を使用します。

- `assets/DX21_RateScale_RS0.opalinevoice`
- `assets/DX21_RateScale_RS1.opalinevoice`
- `assets/DX21_RateScale_RS2.opalinevoice`
- `assets/DX21_RateScale_RS3.opalinevoice`

各音色はOP1キャリアだけを使用し、`AR=31`、`D1R=10`、`D1L=0`、`LevelSc=0`としています。ARは立ち上がりを瞬時にして測定対象から分離し、比較的遅いD1減衰時間を測ります。これにより、音量スケーリングやモジュレーションの影響を受けずにRateScだけを比較できます。

1. 測定音色をOpaline FMへロードする。
2. `VOICE EXPORT`でDX21へ転送する。
3. 録音を開始して、内部測定用の`MIDI KEY TEST`を実行する。
4. RateSc 0、1、2、3について同じ操作を繰り返す。

MIDI Key Testはノート`0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 120, 127`を、各3秒間鳴らします。このボタンはv1.0.9の通常UIでは非表示です。録音ファイル名は`DX21_RateScale_RS0.wav`から`DX21_RateScale_RS3.wav`とします。各ノートの開始点から一定減衰量（例: ピークから-30 dB）へ到達するまでの時間を比較し、実測テーブルを決定します。

2026年7月19日のDX21実機録音から取得した、ピークから`-20 dB`までのD1減衰時間（秒）:

```text
Note     0     12     24     36     48     60     72     84     96    108    120    127
RS 0  1.460  1.455  1.430  1.430  1.150  1.170  0.975  0.975  0.840  0.840  0.840  0.840
RS 1  1.455  1.455  1.415  1.145  0.960  0.825  0.735  0.585  0.490  0.490  0.490  0.420
RS 2  1.455  1.485  1.140  0.825  0.580  0.415  0.290  0.215  0.150  0.155  0.150  0.105
RS 3  1.455  1.455  0.785  0.395  0.200  0.110  0.055  0.030  0.020  0.015  0.015  0.010
```

RS 3ではノート24以降、ほぼ1オクターブごとに減衰時間が半分になります。RS 0も完全な無効ではなく、高音域では弱いレート変化があります。この傾向は現在のキーコード方式と一致します。

```text
keyScaleCode = clamp(round((transposedNote - 24) / 3), 0, 31)
rateOffset = keyScaleCode >> (RateSc xor 3)
```

TL 1ステップは約`0.752575 dB`です。レンダラーは、オペレーターの`0..99` LEVELから整数スケーリング量を先に減算し、実効LEVELをゼロ以上へ制限してからTLへ変換します。これにより、高音域で強いスケーリングを掛けた低LEVELオペレーターを完全に無音化できます。

## LFOとモジュレーション

重要な動作:

- LFO速度は実測値とマニュアルの基準に従い、Speed 35で約6.7 Hz、Speed 99で約55 Hzとなる。
- PMDは直接ピッチ変調の深さを指定する。
- モジュレーションホイールのピッチは、PMDを減らす係数ではなく、独立したMW PITCH経路として加算する。
- PMS=0は単純なピッチ変調OFFではなく、Vibrato OSCの動作に従う。
- Square、PMS=7、PMD=99では、約`-8`から`+8`半音へ到達する。

現在のPMS深度テーブル:

```text
PMS 0: Vibrato OSC、PMS=5相当の実測動作
PMS 1: 0.125半音
PMS 2: 0.25半音
PMS 3: 0.5半音
PMS 4: 1.0半音
PMS 5: 2.0半音
PMS 6: 4.0半音
PMS 7: 8.0半音
```

### モジュレーションホイールのレンジ

- `MOD PITCH`は`0-99`で、PMS適用前のモジュレーションホイール・ピッチ深度を調整する。
- `MOD AMP`は`0-99`で、AMS適用前のモジュレーションホイール・振幅深度を調整する。
- 振幅変調は、AMスイッチが有効なオペレーターだけに作用する。
- 直接LFOのPMD/AMDとモジュレーションホイールのレンジは独立しており、実効深度を加算して`99`を上限とする。
- 音色Aを選択すると、その音色のPMDで`MOD PITCH`、AMDで`MOD AMP`を初期化する。その後は個別に編集できる。
- 音色Bの選択では、共有モジュレーションホイール・レンジを上書きしない。

### ポルタメントモード

- A/Bエンジンは、それぞれ独立したOFF/FULL/FINGERポルタメントモードを保存する。
- `POLY`ではOFF/FULL、`MONO`ではOFF/FULL/FINGERを選択できる。
- FULLは直前に演奏したノートから移動する。FINGERは別の鍵盤を押したまま次の鍵盤を押した場合だけ移動する。
- サステインペダルで保持されたノートは、Fingered Portamentoの物理的な押鍵として数えない。
- `POLY`モードではMIDI `CC65`がPortamento Foot Switchを制御し、`64-127`でFull Time Portamentoを有効、`0-63`で無効にする。
- `MONO`モードのFingered Portamentoは押鍵の重なりで決まるため、MIDI `CC65`では制限しない。

### 並列キャリアの音量補正

- キャリアミキサー後に軽い`carrierCount^-0.18`ゲインを適用する。
- 補正量の目安は、1キャリアで`0 dB`、2キャリアで`-1.1 dB`、3キャリアで`-1.7 dB`、4キャリアで`-2.2 dB`。

## パフォーマンスコントロール

- Pitch Bend Rangeは`0..12`半音、初期値は`2`。`12`ではホイール両端で上下1オクターブ変化する。
- Portamentoは`0..99`。0は最短移動時間で、エフェクトを無効にはしない。
- Portamentoの`1..99`には、約10 msから2秒までの非線形時間カーブを使用する。新しく発音したボイスは直前のノートから開始し、目標ノートへ移動する。
- RANGE、PORTA時間、A/B個別のポルタメントモードは音色パラメーターではなく、パフォーマンス設定としてスタンドアロン/プラグイン状態へ保存する。

## MIDIコントロール

| MIDIメッセージ | 音源動作 |
| --- | --- |
| Note On/Off | SINGLE/DUAL/SPLITとPOLY/MONO状態に従って発音・リリースする |
| Pitch Bend | グローバル`0..12` RANGEを使う双方向ピッチベンド |
| CC1 | MOD PITCH/MOD AMP経路を駆動するモジュレーションホイール |
| CC64 | サステインペダル。踏下中はNote Offを保留する |
| CC65 | POLY時のポルタメント・フットスイッチ。MONO/Fingeredではゲートとして使用しない |
| All Notes Off / All Sound Off | 発音中ボイスとコントローラー保持ノートを消去する |

プラグイン版Standaloneでは、ウィンドウにフォーカスがある間だけPCキーボードによる発音を受け付けます。VST3エディターではDAWのショートカットを維持するため、PC文字キーをノートへ変換しません。どちらもホスト/MIDIノートと画面鍵盤に対応します。

## プラグイン状態

DAWはJUCE `ValueTree`を使用する`getStateInformation()` / `setStateInformation()`経由でプラグイン状態を保存します。

プラグイン版スタンドアロンでは、次の操作も提供します。

- `Save current state...`
- `Load a saved state...`
- `Reset to default state`

状態ファイルの拡張子は`.opalinefmstate`です。

## リリースおよび法的注意事項

v1.0.9公開リリースでは、次のインストーラーを配布します。

- Windows x64スタンドアロン: `OpalineFM-Standalone-v1.0.9-Windows-x64.exe`
- Windows x64 VST3: `OpalineFM-VST3-v1.0.9-Windows-x64.exe`
- 署名・公証済みmacOS Standalone、VST3、Audio Unitパッケージ

Windows VST3版は`C:\Program Files\Common Files\VST3\Opaline FM.vst3`へインストールします。Windows版は現在未署名で、macOS版は署名・公証済みです。公開インストーラーは[公式GitHub Release](https://github.com/Hidecade/OpalineFM/releases/tag/v1.0.9)から取得します。

公開リリース文書には次を記載します。

- Opaline FMはFM音源方式を採用しているが、チップエミュレーションではなく、実際のFM音源機器を完全再現するものではない。
- 互換製品名に言及する場合は、互換性の説明だけを目的とする。
- `assets/factory.syx`は、このプロジェクト用に作成したOpaline FMオリジナルのファクトリー音色である。
- 権利を確認できない第三者のファクトリー音色バンクは再配布しない。
- バイナリー配布前にJUCEライセンス条件を満たす必要がある。
- VST3 SDKおよびその他の第三者ライセンス表示を同梱する。

## 既知の技術的負債

- プラグイン状態とAPVTSの同期経路は実用上動作するが、完全なサンプル精度ではない。
- Dual/Split動作とマニュアルどおりの厳密なLFO共有規則は未完成。
- エンジンにはサンプル単位の`pow`、ボイス終了判定、FX演算が残る。音声出力の回帰比較を追加してから事前計算やゼロ設定時の短絡を行う必要がある。
- iOSの割り込み復帰とAUv3は実装済みだが、電話、Siri、Bluetooth、長時間演奏を含む実機回帰テストが継続して必要である。
- JUCEサブモジュールには、スタンドアロン状態ファイルの拡張子対応のためローカル変更がある。公開時にはフォークとして管理するか、非侵襲的な方法へ置き換える必要がある。
