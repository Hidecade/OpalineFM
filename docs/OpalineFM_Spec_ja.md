# Opaline FM 仕様書

Opaline FMは、C++/JUCEで実装された4オペレーターFMシンセサイザーです。1980年代のクラシックな4オペレーター・デジタルFM楽器を参考にしつつ、実用的な互換音色バンクの取り扱いに重点を置いています。ヤマハの公式製品を意図するものではありません。

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
- Type Aは、従来の互換Opaline信号経路を維持する。
- Type Bは、チップ寄りのオペレーター、アッテネーション、フィードバック、出力経路を使用する。

内部名の`opaline`は、互換レベルのデータ構造とSysExの意味を表すため維持しています。

## 4オペレーターFMアーキテクチャ

各ボイスは4つのサイン波オペレーターを持ちます。各オペレーターは`baseFrequency * ratio + detune`の周波数で信号を生成します。アルゴリズムテーブルによって、その出力を最終キャリアミキサーへ送るか、別のオペレーターの位相変調へ使うかが決まります。

- **キャリア**は可聴出力へ直接加算される。
- **モジュレーター**は後段オペレーターの位相を変え、倍音構成を変化させる。
- オペレーターの役割に応じて、LEVELはキャリア振幅またはモジュレーション指数を制御する。
- オペレーター4がフィードバックループを持ち、FBで量を設定する。
- アルゴリズム1～4は主に直列変調、5～8は並列分岐と複数キャリアを使用する。
- 並列キャリアはボイス出力処理前に加算する。Type Bでは後述の軽いキャリア数補正を適用する。

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

Keyboard Level Scaling（`LevelSc`）は、DX21の録音による実測値に基づきます。MIDIノート0を正規化基準とし、対応する`LevelSc`範囲では整数TLオフセットがゼロになります。減衰量はMIDIノート0より上で1オクターブごとに2倍になります。ホストによってC0/C1などのオクターブ表記が異なるため、仕様ではMIDIノート番号を基準とします。

TLオフセットの計算式:

```text
octaveFactor = 2 ^ (midiNote / 12)
levelScaleTl = floor(LevelSc / 384 * octaveFactor)
levelScaleTl = clamp(levelScaleTl, 0, 127)
```

これは、MIDIノート36を基準にした実測式`floor(LevelSc / 48 * 2 ^ ((midiNote - 36) / 12))`と数学的に等価です。ゼロレベルの動作を明確にするため、MIDIノート0形式を使用します。

MIDIノート36、48、60、72、84、96での実測TL相当減衰量:

```text
LevelSc  36  48  60  72  84  96
      0   0   0   0   0   0   0
     25   0   1   2   4   8  16
     50   1   2   4   8  16  33
     75   1   3   6  12  25  50
     99   1   3   7  16  33  67
```

TL 1ステップは約`0.752575 dB`です。この実測カーブはType AとType Bの両方へ適用します。Type Bでは、オペレーターの`0..99` LEVELから整数スケーリング量を先に減算し、実効LEVELをゼロ以上へ制限してからTLへ変換します。これにより、高音域で強いスケーリングを掛けた低LEVELオペレーターを完全に無音化できます。Type Aでは、同じスケーリング量を互換LEVEL領域へ変換します。整数スケーリング値の切り捨ては、式全体の計算後にのみ行います。

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

- Type Bでは、チップ形式のキャリアミキサー後に軽い`carrierCount^-0.18`ゲインを適用する。
- 補正量の目安は、1キャリアで`0 dB`、2キャリアで`-1.1 dB`、3キャリアで`-1.7 dB`、4キャリアで`-2.2 dB`。
- Type Aは既存のRMS形式キャリア正規化を維持する。

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

公開リリース文書には次を記載します。

- Opaline FMは非公式であり、ヤマハとは提携していない。
- Yamahaおよび互換製品名に言及する場合は、互換性の説明だけを目的とする。
- 権利を確認できないファクトリー音色バンクは再配布しない。
- バイナリー配布前にJUCEライセンス条件を満たす必要がある。
- VST3 SDKおよびその他の第三者ライセンス表示を同梱する。

## 既知の技術的負債

- オーディオスレッドのロックは、今後リアルタイム安全性を改善する必要がある。
- プラグイン状態とAPVTSの同期経路は実用上動作するが、完全なサンプル精度ではない。
- Dual/Split動作とマニュアルどおりの厳密なLFO共有規則は未完成。
- JUCEサブモジュールには、スタンドアロン状態ファイルの拡張子対応のためローカル変更がある。公開時にはフォークとして管理するか、非侵襲的な方法へ置き換える必要がある。
