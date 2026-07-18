# YM2151 / YM2612 音色インポート仕様案

## 1. 目的

Opaline FMへ、レトロゲーム音楽制作や音色アーカイブで流通しているYM2151（OPM）およびYM2612（OPN2）のFM音色データを読み込めるようにする。

対象は「1音色を現在の編集音色へ読み込む」操作とする。Opaline FMはチップエミュレーターではないため、元チップのレジスター値を完全再現する機能ではなく、4オペレーター構造をOpaline FMの音色パラメーターへ変換する機能として実装する。

既存の次の機能とファイル形式は変更しない。

- 互換32音色SysExバンク（`.syx`）
- Opaline単音色（`.opalinevoice`）
- Opaline全ライブラリー（`.opalinelibrary.xml`）
- Windows、macOS、VST3、macOS AU、iOSスタンドアロン、AUv3の既存動作

## 2. 対応範囲

### 2.1 初期対応形式

| 優先度 | 拡張子 | 主な対象 | 内容 | 初期対応 |
|---|---|---|---|---|
| 1 | `.opm` | YM2151 | VOPM互換のテキスト音色リスト | 読み込み |
| 1 | `.tfi` | YM2612 | TFM Music Maker単音色、42 bytes | 読み込み |
| 1 | `.vgi` | YM2612 | VGM Music Maker単音色、43 bytes | 読み込み |
| 1 | `.dmp` | YM2151 / YM2612 | DefleMask FMプリセット | 読み込み |

`OPM`はファイル内に複数音色を格納できる。Opaline FMでは一覧を表示し、ユーザーが選択した1音色だけを現在音色へ読み込む。

### 2.2 初期対応から除外する形式

| 形式 | 除外理由 |
|---|---|
| `.vgm` / `.vgz` | 楽曲の時系列レジスター書き込みログであり、単音色ファイルではない。音色境界や最終状態の抽出規則が別途必要。 |
| `.cym` | YM2151の時系列レジスターログであり、単音色ファイルではない。 |
| `.gyb` / `.wopn` / `.wopn2` | 複数音色バンク。初期版の単音色読み込みとはUIと保存先が異なる。 |
| `.fui` / `.fur` | Furnace固有形式。マクロなどOpaline FMにない情報を含み、形式の世代管理も必要。 |
| `.y12` / `.tyi` / `.eif` | 流通量と相互運用性が上記4形式より低いため後回しとする。 |
| ROM、実行ファイル、サウンドドライバー固有データ | ゲームごとに配置・圧縮・オペレーター順が異なり、自動判定できない。 |

将来、VGM/VGZから音色を抽出する場合は、本仕様のパーサーへ直接追加せず、独立した「音色抽出」機能として設計する。

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
- YM2151かYM2612かをファイルから確定できない世代では、DT2の使用などから勝手に断定しない。ユーザー選択または`Auto`を用意する。
- 新しいDMP世代を追加するときは、DefleMaskまたはFurnaceの実ファイルによるfixture testを必須とする。

## 4. 内部中間形式

各ファイルパーサーは、直接`OpalinePatch`を生成せず、次の意味を持つチップ音色中間形式を返す。

```cpp
enum class ChipFamily { ym2151, ym2612, unknown };

struct ChipFmOperator
{
    int multiplier;
    int detune1;
    int detune2;
    int totalLevel;
    int rateScale;
    int attackRate;
    int decay1Rate;
    int decay2Rate;
    int releaseRate;
    int sustainLevel;
    int ssgEg;
    bool ampModEnable;
};

struct ChipFmVoice
{
    ChipFamily family;
    std::string name;
    int algorithm;
    int feedback;
    int lfoSpeed;
    int ampModDepth;
    int pitchModDepth;
    int lfoWave;
    int ampModSensitivity;
    int pitchModSensitivity;
    std::array<ChipFmOperator, 4> operators;
};
```

実装時には、値の有無を区別できるようにLFOなど形式依存項目を`std::optional`にしてよい。パーサー層は構文と範囲を検証し、変換層は中間形式から`OpalinePatchWithMetadata`を生成する。

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

- 既存の単音色`LOAD`ファイル選択を次へ拡張する。
- 表示名: `Load Single Voice`
- 対象: `*.opalinevoice;*.opm;*.tfi;*.vgi;*.dmp`
- TFI/VGI/DMPは読込後に変換結果を現在音色へ適用する。
- OPMが複数音色を含む場合は、番号と音色名の一覧をモーダル表示し、1音色を選択する。
- 読み込み成功後も自動STOREは行わない。ユーザーが試奏し、既存の`STORE`で保存する。

### 7.2 iOSスタンドアロン

- 既存の単音色ファイルインポーターへ`.opm`、`.tfi`、`.vgi`、`.dmp`のUTTypeを追加する。
- セキュリティスコープ内でデータを読み終えた後、共有C++パーサーへ渡す。
- OPMが複数音色の場合はSwiftUIの音色選択シートを表示する。
- 成功時は`<format> voice loaded`、近似がある場合は短い警告をAudio Statusへ表示する。
- 読み込んだ音色は現在のユーザーライブラリーへ自動保存せず、既存のSTORE操作に従う。

### 7.3 AUv3

AUv3画面にはファイル読み込みを追加しない。ホストのセキュリティスコープ、状態復元、拡張プロセスの安定性を優先し、音色変換はスタンドアロン版で行う。変換後の音色をAUv3へ共有する仕組みは別仕様とする。

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
- Noise設定を無視した
- 未対応LFO波形を既定値へ置換した
- RatioがOpalineの最大値へ制限された
- 入力形式からチップ種別を確定できなかった

APIは表示用文字列だけでなく、テスト可能な警告コードも返す。

## 9. 実装構成案

共有エンジンにJUCE/UIKit非依存の実装を置く。

```text
Source/Engine/ChipVoiceImport.h
Source/Engine/ChipVoiceImport.cpp
Source/Engine/ChipVoiceFormats/OpmVoiceParser.cpp
Source/Engine/ChipVoiceFormats/TfiVgiVoiceParser.cpp
Source/Engine/ChipVoiceFormats/DmpVoiceParser.cpp
```

公開API案:

```cpp
struct ChipVoiceImportResult
{
    std::vector<OpalinePatchWithMetadata> voices;
    std::vector<ChipVoiceImportWarning> warnings;
    ChipVoiceFormat format;
};

ChipVoiceImportResult importChipVoices(
    const std::vector<std::uint8_t>& data,
    ChipVoiceFormat format,
    const std::string& fallbackName,
    ChipFamily familyHint = ChipFamily::unknown);
```

- ファイルアクセスと選択UIは各アプリ層が担当する。
- パースと変換は共有C++だけで行い、Windows/macOS/iOSで同じ結果を得る。
- 例外は共有API境界で捕捉可能な形式に統一し、音声スレッドから呼ばない。
- 読み込み前にAll Notes Offを行うのは、パース成功後かつ音色適用直前だけとする。
- 変換中およびファイルアクセス中に音声レンダー用mutexを保持しない。

## 10. テスト仕様

### 10.1 パーサーユニットテスト

- OPM: コメント、空行、複数音色、ラベル順、負数、不正行、長い音色名
- TFI: 42 bytes正常系、41/43 bytes拒否、全最小値、全最大値
- VGI: 43 bytes正常系、FMS/AMS抽出、サイズ不一致拒否
- DMP: v10/v11 FM正常系、Standard音色拒否、未対応バージョン拒否
- 各形式: Algorithm 0...7とFeedback 0...7
- オペレーター順: 各OPだけTLを変えたfixtureで対応先を検証

### 10.2 変換テスト

- MUL 0がRatio 0.5になる
- MUL 1...15が対応する整数Ratioになる
- YM2151 DT2 0...3が期待する近似Ratioになる
- TL 0がLEVEL 99、TL 99以上がLEVEL 0になる
- SL 0がD1L 15、SL 15がD1L 0になる
- AR/DR/SR/RR/RSが境界値を維持する
- SSG-EG、Noise、Ratio上限で警告が返る
- 変換後の全値が`normalizePatch()`の範囲内にある
- 同じ入力から全プラットフォームで同じ`OpalinePatch`を得る

### 10.3 回帰テスト

- 既存`.opalinevoice`の読み込みと保存
- 既存`.syx`の32音色読み込みと保存
- `.opalinelibrary.xml`の8バンク読み込みと保存
- Factory BankとFX情報の復元
- VST3、macOS AU、AUv3の状態復元

### 10.4 手動試奏

- YM2151の代表的なOPM音色を原音と比較する。
- YM2612の代表的なTFI/VGI音色を原音と比較する。
- キャリアとモジュレーターの入れ替わりがないことを確認する。
- 高TL音色で過大音量やクリッピングが発生しないことを確認する。
- 読み込み失敗後も元の音色を演奏できることを確認する。

## 11. 完了条件

- [x] OPM、TFI、VGI、対応DMPから現在音色へ読み込める。
- [x] OPMの複数音色から1音色を選択できる（macOS/Windows版）。
- [ ] macOSとWindowsのスタンドアロン版で同じ変換結果になる。
- [x] iOSスタンドアロン版でFilesから読み込める。複数音色OPMは先頭音色を読み込む。
- [x] AUv3ターゲットを含むiOSビルドが成功する。
- [ ] 未対応パラメーターが警告として確認できる。
- [x] 不正ファイルを適用前に拒否し、現在音色を変更しない。
- [ ] 既存ファイル形式と全ターゲットの回帰テストが通る。

### 実装結果（2026-07-17）

- 単音色LOADを拡張子判定へ変更し、`.opalinevoice`、`.opm`、`.tfi`、`.vgi`、`.dmp`へ対応した。
- チップ音色ではFXをOFF、Reverb/Delay/Chorus/Mixを0、Toneを50、Pitch EGをニュートラルへ初期化する。
- 共通C++変換器をmacOS/WindowsとiOSから利用する構成にした。
- DMPはVersion 10の50-byte FMプリセットと、system byteを持つVersion 11の51-byte FMプリセットに対応する。
- macOS Standalone、VST3、AU、iOS Simulator（スタンドアロン＋AUv3）のビルドに成功した。
- エンジンユニットテストにTFI、VGI、OPM、DMPの正常系、変換値、異常サイズ拒否を追加し、成功した。
- 2026-07-18に`build`を削除した状態からmacOS Releaseを再構成し、Standalone、プラグイン版Standalone、VST3、AUのクリーンビルドとエンジンテスト（1/1）に成功した。
- 音色変換とファイルアクセスはUI側で実行し、変換結果は`RealtimeStateMailbox`経由で音声レンダーへ反映するため、音声レンダー用mutexを保持しない。

### 残課題

- Windows実機ビルドと、各形式の実ファイルによる試奏確認。
- iOSで複数音色OPMを選択するUI。
- 新しいDMPバージョンへの対応。
- SSG-EGやDT2近似警告のiOS画面表示。

## 12. 実装順

1. 中間形式、警告型、TFI/VGIパーサーを実装する。
2. 共通変換層とユニットテストを実装する。
3. OPMパーサーと複数音色選択を実装する。
4. DMP v10 FMパーサーをfixture付きで実装する。
5. macOS/Windowsの既存LOADへ接続する。
6. iOSスタンドアロンのDocument Pickerへ接続する。
7. 回帰ビルドと実機試奏を行う。
8. 実際に流通しているファイルを確認し、追加DMP世代またはバンク形式を次期仕様へ切り出す。

## 13. 著作権と配布

インポート機能はユーザーが権利を持つ、または利用許諾を得た音色データを変換するための機能とする。市販ゲームから抽出された音色ファイルをOpaline FM本体、Factory Bank、テストfixture、GitHub Releaseへ同梱しない。テストデータは本プロジェクトで新規作成したもの、または再配布条件が明確なものだけを使用する。

## 14. 参考資料

- [OPM File Format - VGMRips](https://vgmrips.net/wiki/OPM_File_Format)
- [TFI and VGI formats - Plutiedev](https://www.plutiedev.com/format-tfi)
- [DMP File Format - VGMRips（Delekによる仕様の転載）](https://vgmrips.net/wiki/DMP_File_Format)
- [DefleMask Tracker Manual](https://www.deflemask.com/manual.pdf)
- [Furnace instrument editor documentation](https://tildearrow.org/furnace/doc/v0.6/4-instrument/)
- [Furnace source repository](https://github.com/tildearrow/furnace)
