# YM2151 / YM2612 音色インポート仕様

## 1. 目的

Opaline FMは、レトロゲーム音楽制作や音色アーカイブで流通しているYM2151（OPM）およびYM2612（OPN2）のFM音色データを読み込みます。

対象は「1音色を現在の編集音色へ読み込む」操作とする。Opaline FMはチップエミュレーターではないため、元チップのレジスター値を完全再現する機能ではなく、4オペレーター構造をOpaline FMの音色パラメーターへ変換する機能として実装する。

既存の次の機能とファイル形式は変更しない。

- 互換32音色SysExバンク（`.syx`）
- Opaline単音色（`.opalinevoice`）
- Opaline全ライブラリー（`.opalinelibrary.xml`）
- Windows、macOS、VST3、macOS AU、iOSスタンドアロン、AUv3の既存動作

## 2. 対応範囲

### 2.1 対応形式

| 拡張子 | 主な対象 | 内容 | 動作 |
|---|---|---|---|
| `.opm` | YM2151 | VOPM互換のテキスト音色リスト | 読み込み |
| `.tfi` | YM2612 | TFM Music Maker単音色、42 bytes | 読み込み |
| `.vgi` | YM2612 | VGM Music Maker単音色、43 bytes | 読み込み |
| `.dmp` | YM2151 / YM2612 | DefleMask FMプリセット | 読み込み |

`OPM`はファイル内に複数音色を格納できます。macOS/Windows版は一覧から選択した1音色を読み込み、iOS版は先頭音色を読み込みます。

### 2.2 非対応形式

| 形式 | 除外理由 |
|---|---|
| `.vgm` / `.vgz` | 楽曲の時系列レジスター書き込みログであり、単音色ファイルではない。音色境界や最終状態の抽出規則が別途必要。 |
| `.cym` | YM2151の時系列レジスターログであり、単音色ファイルではない。 |
| `.gyb` / `.wopn` / `.wopn2` | 複数音色バンクであり、単音色インポーターの対象外。 |
| `.fui` / `.fur` | Furnace固有形式。マクロなどOpaline FMにない情報を含み、形式の世代管理も必要。 |
| `.y12` / `.tyi` / `.eif` | 現行インポーターの対象外。 |
| ROM、実行ファイル、サウンドドライバー固有データ | ゲームごとに配置・圧縮・オペレーター順が異なり、自動判定できない。 |

## 3. 形式概要

### 3.1 OPM（YM2151 / VOPM）

プレーンテキストで、コメントは`//`から行末までとする。基本ブロックは次の構造を持つ。

```text
@:voice_number voice_name
LFO: LFRQ AMD PMD WF NFRQ
CH: PAN FL CON AMS PMS SLOT NE
M1: AR D1R D2R RR D1L TL KS MUL DT1 DT2 AME
C1: AR D1R D2R RR D1L TL KS MUL DT1 DT2 AME
M2: AR D1R D2R RR D1L TL KS MUL DT1 DT2 AME
C2: AR D1R D2R RR D1L TL KS MUL DT1 DT2 AME
```

- すべての値は10進数として読む。
- 空行とコメント行を許可する。
- ラベルは大文字・小文字を区別しない。
- 演算子行の順序は`M1, C1, M2, C2`を標準とするが、ラベルを基準に関連付ける。
- `@:`ごとに1音色として確定する。
- 音色名は前後空白を除去し、Opaline FMへ格納するときUTF-8の先頭10文字へ制限する。
- 不明な行は警告として記録し、必須行が揃っていれば読み込みを継続する。

### 3.2 TFI（YM2612）

固定長42 bytesのバイナリー形式とする。

```text
offset  size  value
0       1     Algorithm (0...7)
1       1     Feedback (0...7)
2       40    4 operator blocks, 10 bytes each
```

各オペレーターブロック:

```text
MUL, DT, TL, RS, AR, DR, SR, RR, SL, SSG-EG
```

ファイル内オペレーター順は`S1, S3, S2, S4`とする。ファイルサイズが42 bytesでない場合は読み込みを失敗させる。音色名はファイル名から取得する。

### 3.3 VGI（YM2612）

固定長43 bytesのバイナリー形式とする。TFIのAlgorithm、Feedbackの直後に、YM2612の`B4`レジスター相当のFMS/AMS byteが1つ入る。それ以外はTFIと同じとする。

```text
offset  size  value
0       1     Algorithm
1       1     Feedback
2       1     FMS / AMS
3       40    4 operator blocks
```

ファイルサイズが43 bytesでない場合は読み込みを失敗させる。音色名はファイル名から取得する。

### 3.4 DMP（DefleMask）

DMPは先頭のバージョンと音色モードを検査し、FM音色だけを受け付ける。現行実装は`FILE_VERSION = 10 (0x0A)`の50-byte形式と、system byteが追加された`FILE_VERSION = 11 (0x0B)`の51-byte形式に対応する。

```text
Version 10: VERSION, MODE, FMS/PMS, FB, ALG, AMS, 4 * OP(11 bytes)
Version 11: VERSION, SYSTEM, MODE, FMS/PMS, FB, ALG, AMS, 4 * OP(11 bytes)
```

Version 11のSYSTEMは、たとえば`2 = YM2612/Genesis`、`8 = YM2151`である。現在は形式判定とヘッダー位置の決定に使用し、Opaline音色パラメーターとしては保存しない。

FMデータは次の論理項目として読む。

```text
FMS/PMS, Feedback, Algorithm, AMS
4 * (MUL, TL, AR, DR, SL, RR, AM, RS, DT, D2R, SSG-EG)
```

- Standard/PSG音色はエラーにする。
- バージョンごとに個別パーサーを用意し、サイズだけでバージョンを推測しない。
- 未対応バージョンは`Unsupported DMP version`として安全に拒否する。
- Version 11のSYSTEMはヘッダー解釈にだけ使用し、DT2などの値からチップ種別を推測しない。

## 4. 内部中間形式

各ファイルパーサーは、直接`OpalinePatch`を生成せず、`ChipVoiceImport.cpp`内の次の中間形式へデータを格納します。

```cpp
struct ChipOperator
{
    int multiplier = 1;
    int detune = 0;
    int detune2 = 0;
    int totalLevel = 127;
    int rateScale = 0;
    int attackRate = 0;
    int decay1Rate = 0;
    int decay2Rate = 0;
    int releaseRate = 0;
    int sustainLevel = 15;
    int ssgEg = 0;
    bool ampModEnable = false;
    bool enabled = true;
};

struct ChipVoice
{
    std::string name;
    int algorithm = 0;
    int feedback = 0;
    int lfoSpeed = 0;
    int ampDepth = 0;
    int pitchDepth = 0;
    int lfoWave = 0;
    int ampSensitivity = 0;
    int pitchSensitivity = 0;
    std::array<ChipOperator, 4> operators {};
};
```

パーサー層は構文と範囲を検証し、共通変換層はこの中間形式から`OpalinePatchWithMetadata`を生成します。

## 5. パラメーター変換

### 5.0 変換処理の順序

データ変換は、ファイル形式ごとの差をパーサーで吸収した後、全形式で共通の変換処理を行う。

1. 拡張子に対応するパーサーで値と範囲を検証する。
2. ファイル固有のオペレーター順をOpalineの`OP1...OP4`へ並べ替える。
3. MULとDT2から最も近いOpaline Ratio Indexを求める。
4. DT1、TL、EG、LFOなどをOpalineの値域へ変換する。
5. 入力形式に存在しないPitch EG、FX、Level Scale、Velocityなどを初期値へ戻す。
6. 最後に`normalizePatch()`を通し、全パラメーターをOpalineの有効範囲内へ制限する。

変換はファイル読み込み時に1回だけ実行し、音声レンダリング中には実行しない。変換または検証に失敗した場合は、現在の音色へ途中結果を適用しない。

### 5.1 アルゴリズムとフィードバック

- 入力Algorithm `0...7`をOpaline `1...8`へ変換する。
- 8種類の接続は同じ番号のトポロジーへ割り当てる。
- Feedback `0...7`は同じ値を使用する。
- フィードバック対象はOpaline FMのOP4へ割り当てる。

### 5.2 オペレーター順

変換後の論理順を`OP1, OP2, OP3, OP4`へ統一する。

| 入力形式 | 入力順 | Opaline順 |
|---|---|---|
| OPM | M1, C1, M2, C2 | OP1, OP2, OP3, OP4へトポロジーを維持する対応表を使用 |
| TFI/VGI | S1, S3, S2, S4 | YM2612レジスター順を論理OP1...OP4へ並べ替える |
| DMP | 形式バージョンの定義順 | バージョン別対応表で並べ替える |

並べ替えはパーサー内の固定配列として定義し、UIコードへ分散させない。アルゴリズムごとのキャリア位置をfixture testで検証する。

現在の実装で使用する具体的な対応は次のとおり。Opalineは画面上の信号方向に合わせて、Yamaha形式とは逆向きにOP番号を割り当てている。

| 形式 | ファイル内の演算子 | Opaline演算子 |
|---|---|---|
| OPM | M1, C1, M2, C2 | OP4, OP2, OP3, OP1 |
| TFI/VGI | S1, S3, S2, S4 | OP4, OP2, OP3, OP1 |
| DMP v10/v11 | OP1, OP3, OP2, OP4 | OP4, OP2, OP3, OP1 |

OPM、TFI/VGI、DMPはいずれもファイル内ではYamahaのレジスター／スロット順に相当する`OP1, OP3, OP2, OP4`で並ぶ。OPMのラベルでは同じ順序が`M1, C1, M2, C2`と表される。このため中央の2演算子を論理順へ戻したうえで、Opalineの信号方向に合わせてOP番号を反転する。Algorithmは入力`0...7`をOpaline表示`1...8`へ変換するだけで、上記の演算子並べ替えによって接続関係とキャリア位置を維持する。

### 5.3 MUL、DT1、DT2とRatio

YM2151/YM2612のMULは、`0 = 0.5`、`1...15 = その整数倍率`として扱う。

YM2151のDT2は独立したUI項目として保持せず、MULとの積をRatioへ合成する。目標倍率を次で求め、`opalineRatios()`にある64値のうち対数周波数差が最小のRatio Indexを選ぶ。

```text
baseRatio = (MUL == 0) ? 0.5 : MUL
dt2Factor = table(DT2)  // 1.0, sqrt(2), 1.57, 1.73
targetRatio = baseRatio * dt2Factor
distance(i) = abs(log2(opalineRatios[i] / targetRatio))
ratioIndex = argmin(distance(i)), i = 0...63
```

DT2係数は次の固定値を使用する。

| DT2 | 係数 | おおよその音程差 |
|---:|---:|---:|
| 0 | `1.0` | 0 cent |
| 1 | `1.41421356237` | 600 cent |
| 2 | `1.57` | 約781 cent |
| 3 | `1.73` | 約949 cent |

周波数比は乗算で変化するため、単純な差`abs(ratio - targetRatio)`ではなく、音程差に相当する対数距離を使う。同距離の候補がある場合は、Ratio表を小さいIndexから検索し、先に見つかった小さいRatioを選ぶ。最小距離が`0.0001 octave`を超える場合は近似変換として警告を記録する。

Opaline Ratio表は次の64値である。表中の番号はRatio Index。

```text
 0:0.50   1:0.71   2:0.78   3:0.87   4:1.00   5:1.41   6:1.57   7:1.73
 8:2.00   9:2.82  10:3.00  11:3.14  12:3.46  13:4.00  14:4.24  15:4.71
16:5.00  17:5.19  18:5.65  19:6.00  20:6.28  21:6.92  22:7.00  23:7.07
24:7.85  25:8.00  26:8.48  27:8.65  28:9.00  29:9.42  30:9.89  31:10.00
32:10.38 33:10.99 34:11.00 35:11.30 36:12.00 37:12.11 38:12.56 39:12.72
40:13.00 41:13.84 42:14.00 43:14.10 44:14.13 45:15.00 46:15.55 47:15.57
48:15.70 49:16.96 50:17.27 51:17.30 52:18.37 53:18.84 54:19.03 55:19.78
56:20.41 57:20.76 58:21.20 59:21.98 60:22.49 61:23.55 62:24.22 63:25.95
```

代表的な近似結果は次のとおり。

| MUL | DT2 | targetRatio | 採用Ratio | Index |
|---:|---:|---:|---:|---:|
| 0 | 0 | 0.500 | 0.50 | 0 |
| 0 | 1 | 0.707 | 0.71 | 1 |
| 0 | 2 | 0.785 | 0.78 | 2 |
| 0 | 3 | 0.865 | 0.87 | 3 |
| 1 | 1 | 1.414 | 1.41 | 5 |
| 2 | 2 | 3.140 | 3.14 | 11 |
| 4 | 3 | 6.920 | 6.92 | 21 |
| 8 | 1 | 11.314 | 11.30 | 35 |
| 15 | 3 | 25.950 | 25.95 | 63 |

DT1はRatioへ合成せず、符号付き`-3...+3`へ復号してOpaline `detune`へ設定する。TFI/VGIおよびDMPでは中央基準の格納値を使用し、`0...6`に対して`detune = DT - 3`を使う。DMPの互換値`7`はDetune `0`として扱う。

| TFI/VGI/DMPのDT | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7（DMPのみ） |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Opaline detune | -3 | -2 | -1 | 0 | +1 | +2 | +3 | 0 |

OPMだけはYM2151レジスター形式なので、次のように復号する。

| 入力DT1 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Opaline detune | 0 | +1 | +2 | +3 | 0 | -1 | -2 | -3 |

DMP v10/v11ではDT byteの下位nibbleを中央基準のDT1、bit 4...5をDT2として読む。TFI/VGIにはDT2がないため常に`0`とする。

### 5.4 Total Level

チップ側TLは`0 = 最大音量`、`127 = 無音`であり、Opaline LEVELは`0 = 無音`、`99 = 最大音量`なので反転する。

```text
opalineLevel = 99 - min(TL, 99)
```

TLは変換前に`0...127`であることを検証する。`0...99`は値を反転し、`100...127`はOpaline LEVEL `0`へまとめる。代表値は次のとおり。

| チップTL | Opaline LEVEL |
|---:|---:|
| 0 | 99 |
| 16 | 83 |
| 32 | 67 |
| 64 | 35 |
| 96 | 3 |
| 99 | 0 |
| 112 | 0 |
| 127 | 0 |

この変換は、チップ側の減衰量をOpalineの`0...99`表示へ直接反転するものである。TL `100...127`の微小音量差は保持しない。チップとOpalineでは発振器、アルゴリズム内の加算、最終ゲインが完全には同一でないため、出力音量や変調量の完全一致を保証するものではない。

### 5.5 エンベロープ

| チップ項目 | Opaline項目 | 変換 |
|---|---|---|
| AR `0...31` | Attack Rate | 同値 |
| DR/D1R `0...31` | Decay 1 Rate | 同値 |
| SR/D2R `0...31` | Decay 2 Rate | 同値 |
| RR `0...15` | Release Rate | 同値 |
| SL/D1L `0...15` | Decay 1 Level | `15 - SL` |
| RS/KS `0...3` | Rate Scale | 同値 |

SSG-EGは現在のOpaline音源に対応項目がないため無視し、値が有効な音色では警告を表示する。

AR、DR、SR、RR、RSはスケーリングせず整数値をそのままコピーする。SL/D1Lだけは値の方向が逆なので、次の式で反転する。

```text
opalineDecay1Level = 15 - min(SL, 15)
```

たとえば`SL 0 → D1L 15`、`SL 8 → D1L 7`、`SL 15 → D1L 0`となる。Type BのチップEGでは、表示値から復元したSLをYM2151の内部値へ直接変換する。通常の`SL 0...14`は`SL * 32`、特殊値`SL 15`は`31 * 32 = 992`として扱う。SL 15を通常式の`15 * 32`として扱わないことが重要である。

Type BのEG実効レートは、YM2151と同じ整数式を使用する。

```text
Attack/Decay/Sustain effectiveRate = rate * 2 + KSR
Release effectiveRate = RR * 4 + 2 + KSR
```

レジスター値を変換して保持するが、SSG-EG動作には対応しない。入力形式別の対応は次のとおり。

| 形式 | Attack | Decay 1 | Decay 2 | Release | Decay 1 Level | Rate Scale |
|---|---|---|---|---|---|---|
| OPM | AR | D1R | D2R | RR | D1L | KS |
| TFI/VGI | AR | DR | SR | RR | SL | RS |
| DMP v10/v11 | AR | DR | D2R | RR | SL | RS |

### 5.6 LFOとAM

- OPMのLFRQ、AMD、PMD、WF、AMS、PMSは、各範囲を検証してOpaline LFOへ設定する。
- VGI/DMPのFMS/PMSとAMSは感度へ設定する。
- TFI/VGI/DMPにはLFO速度とDepthがないため、LFO Speed/Depthは0、SyncはOFFとする。
- オペレーターAMフラグは`ampModEnable`へ設定する。
- OPMのLFO波形`WF 0...3`は同じ番号のOpaline波形へ割り当てる。
- OPMのnoise enable/frequencyは現在のOpaline音源にないため無視する。

OPMの連続値は次の式でOpalineの`0...99`へ変換する。

```text
LFO Speed   = lround(LFRQ * 99.0 / 255.0)
AM Depth    = lround(AMD  * 99.0 / 127.0)
Pitch Depth = lround(PMD  * 99.0 / 127.0)
```

OPMのWF、AMS、PMS、およびVGI/DMPのAMS、PMS/FMSは有効範囲がOpalineと同じため、そのままコピーする。VGIの感度byteは`PMS/FMS = byte & 0x07`、`AMS = (byte >> 4) & 0x03`として分離する。TFI、VGI、DMPにLFO速度またはDepthが存在しない場合は0とする。

OPMのCH行にあるSLOT maskは各Opaline演算子のEnabledへ変換する。PANおよびNoise設定は現在の音色パラメーターへ保存しない。

### 5.7 Opaline固有項目の初期値

入力に存在しない項目は次で初期化し、直前の音色の値を残さない。

- Transpose: `0`
- Pitch EG: ニュートラル
- Keyboard Level Scale: `0`
- Velocity Sensitivity: `0`
- Reverb / Delay / Chorus / Mix: `0`
- Tone: `50`
- FX Enabled: `false`
- オペレーターEnabled: `true`
- LFO Sync: `false`
- VMEM backup: なし（`hasVmem = false`）

## 6. 形式判定と検証

1. 拡張子を候補選択に使う。
2. バイナリー形式は固定長、バージョン、モード、全フィールド範囲を検証する。
3. OPMはUTF-8またはASCIIテキストとして読み、必須ラベルを検証する。
4. 拡張子と内容が矛盾する場合は自動的に別形式として読み替えず、明確なエラーを返す。
5. 1 MBを超える単音色ファイルは拒否する。OPMも音色数を最大1024件へ制限する。
6. 途中までのデータ、整数オーバーフロー、過剰な行長、NULを含む不正テキストを安全に拒否する。
7. パース失敗時は現在音色、ライブラリー、発音状態を変更しない。

## 7. UI仕様

### 7.1 macOS / Windowsスタンドアロン

- 単音色`LOAD`ファイル選択は次の形式を受け付ける。
- 表示名: `Load Single Voice`
- 対象: `*.opalinevoice;*.opm;*.tfi;*.vgi;*.dmp`
- TFI/VGI/DMPは読込後に変換結果を現在音色へ適用する。
- OPMが複数音色を含む場合は、番号と音色名の一覧をモーダル表示し、選択した1音色を適用する。
- 読み込み成功後も自動STOREは行わず、保存には既存の`STORE`を使用する。

### 7.2 iOSスタンドアロン

- 単音色ファイルインポーターは`.opm`、`.tfi`、`.vgi`、`.dmp`のUTTypeを受け付ける。
- セキュリティスコープ内で読み込んだデータを共有C++パーサーへ渡す。
- 複数音色を含むOPMは先頭音色を現在音色へ適用する。
- 成功時は形式名をAudio Statusへ表示し、読み込んだ音色は自動保存しない。

### 7.3 AUv3

AUv3画面はファイル読み込みを提供しません。チップ音色の変換はmacOS、Windows、iOSのスタンドアロン版で行います。

## 8. エラーと警告

### エラー（適用しない）

- 未対応拡張子
- 固定長不一致
- 未対応DMPバージョンまたは非FMモード
- OPM必須行不足
- Algorithm、Feedback、オペレーター値の範囲外
- 読み込み上限超過

### 警告（適用する）

- YM2151 DT2を最も近いRatioへ丸めた
- SSG-EGを無視した

APIは適用された近似や省略を`std::vector<std::string>`の表示用警告として返します。

## 9. 実装構成

JUCE/UIKitに依存しないパーサーと変換処理を共有エンジンに実装しています。OPM、TFI/VGI、DMPの形式別パーサーは`ChipVoiceImport.cpp`内の非公開関数です。

```text
Source/Engine/ChipVoiceImport.h
Source/Engine/ChipVoiceImport.cpp
```

公開API:

```cpp
enum class ChipVoiceFormat
{
    Opm,
    Tfi,
    Vgi,
    Dmp
};

struct ChipVoiceImportResult
{
    ChipVoiceFormat format = ChipVoiceFormat::Tfi;
    std::vector<OpalinePatchWithMetadata> voices;
    std::vector<std::string> warnings;
};

bool isChipVoiceExtension(const std::string& extension);
ChipVoiceImportResult importChipVoices(const std::vector<std::uint8_t>& bytes,
                                       const std::string& extension,
                                       const std::string& fallbackName);
```

- ファイルアクセスと選択UIは各アプリ層が担当します。
- パースと変換は共有C++で行い、Windows、macOS、iOSで同じ変換結果を使用します。
- 変換はUI側で実行し、結果を`RealtimeStateMailbox`経由で音声レンダーへ反映します。
- 変換中とファイルアクセス中は音声レンダー用mutexを保持しません。

## 10. 自動テスト

`Source/Tests/EngineTests.cpp`の`testChipVoiceImport()`は、次の実装済み経路を検証します。

- TFIのAlgorithm、Feedback、オペレーター順、TL、SL/D1L、Pitch EG、FX初期値
- VGIのFMS/AMS
- OPMの音色名、チャンネル値、オペレーター順、TL、D1L、AM、LFO
- DMP Version 10/11のヘッダー、オペレーター順、Detune
- 41-byteの不正TFIデータの拒否

## 11. 実装済み動作

- 単音色`LOAD`は拡張子で形式を判定し、`.opalinevoice`、`.opm`、`.tfi`、`.vgi`、`.dmp`を読み込みます。
- チップ音色ではFXをOFF、Reverb/Delay/Chorus/Mixを0、Toneを50、Pitch EGをニュートラルへ初期化します。
- DMP Version 10の50-byte FMプリセットと、system byteを持つVersion 11の51-byte FMプリセットを読み込みます。
- macOS/Windowsでは複数音色OPMから選択でき、iOSでは先頭音色を読み込みます。
- パース失敗時は現在音色を変更しません。

## 12. 著作権と配布

インポート機能はユーザーが権利を持つ、または利用許諾を得た音色データを変換するための機能とする。市販ゲームから抽出された音色ファイルをOpaline FM本体、Factory Bank、テストfixture、GitHub Releaseへ同梱しない。テストデータは本プロジェクトで新規作成したもの、または再配布条件が明確なものだけを使用する。

## 13. 参考資料

- [OPM File Format - VGMRips](https://vgmrips.net/wiki/OPM_File_Format)
- [TFI and VGI formats - Plutiedev](https://www.plutiedev.com/format-tfi)
- [DMP File Format - VGMRips（Delekによる仕様の転載）](https://vgmrips.net/wiki/DMP_File_Format)
- [DefleMask Tracker Manual](https://www.deflemask.com/manual.pdf)
- [Furnace instrument editor documentation](https://tildearrow.org/furnace/doc/v0.6/4-instrument/)
- [Furnace source repository](https://github.com/tildearrow/furnace)
