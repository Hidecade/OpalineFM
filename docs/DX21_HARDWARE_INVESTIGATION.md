# DX21 実機調査記録

このドキュメントは、実機 Yamaha DX21 で確認した測定値・挙動を記録する。
DX21Native の New エンジンを実機寄せするための一次情報として扱う。

## LFO Speed

### 説明書記載値

DX21 の説明書では、LFO Speed について次のように記載されている。

| LFO Speed | 周波数 |
|---:|---:|
| 0 | 最も遅い |
| 35 | 6.7 Hz |
| 99 | 55 Hz |

解釈:

- `LFO SPEED` は `0 ～ 99` の範囲で設定する。
- `35` で `6.7Hz`、`99` で `55Hz` が説明書上の基準点。
- DX21Native / WebDX21 系の式は、この2点に一致する。

```text
normalized = LFO_SPEED / 99
frequencyHz = 55 * normalized ^ 2.0246997291383155
```

確認:

```text
LFO_SPEED=35 -> 6.7 Hz
LFO_SPEED=99 -> 55 Hz
```

## LFO ピッチモジュレーション

### Square 波形のピッチ変化量

実測:

| LFO Wave | PMS | PMD | 音程変化 |
|---|---:|---:|---|
| Square | 7 | 99 | -8 ～ +8 semitones |
| Square | 7 | 75 | -6 ～ +6 semitones |
| Square | 7 | 50 | -4 ～ +4 semitones |
| Square | 7 | 25 | -2 ～ +2 semitones |
| Square | 6 | 99 | -4 ～ +4 semitones |

補足:

- Triangle / Saw もピッチ変化の最大振れ幅は Square と同じとして扱う。
- つまり、同じ `PMS` / `PMD` であれば、波形が違っても最大到達ピッチは同じになる。
- Square は瞬時に正負へ切り替わり、Triangle / Saw はその範囲内を連続的に移動する。

解釈:

- `WAVE=SQUARE`, `PMS=7`, `PMD=99` では、実機 DX21 は約 `-8 ～ +8 semitones` のピッチ変化になる。
- `WAVE=SQUARE`, `PMS=7`, `PMD=75` では、実機 DX21 は約 `-6 ～ +6 semitones` のピッチ変化になる。
- `WAVE=SQUARE`, `PMS=7`, `PMD=50` では、実機 DX21 は約 `-4 ～ +4 semitones` のピッチ変化になる。
- `WAVE=SQUARE`, `PMS=7`, `PMD=25` では、実機 DX21 は約 `-2 ～ +2 semitones` のピッチ変化になる。
- `WAVE=SQUARE`, `PMS=6`, `PMD=99` では、実機 DX21 は約 `-4 ～ +4 semitones` のピッチ変化になる。
- `75 / 99 * 8 = 6.06` なので、PMD は少なくともこの範囲ではほぼ線形に見える。
- `PMS=6`, `PMD=99` が `-4 ～ +4 semitones` であれば、PMS は線形ではなく段階的または非線形の可能性が高い。
- エンジンのピッチ LFO 深さは、PMDを線形、PMSを実測テーブルとして扱うのが妥当。

```text
pitchDepthSemitones = PMD / 99 * pmsDepthSemitones[PMS]
```

### エンジン調整メモ

PMD方向の実測値は WebDX21 JS/FM のLFO深さモデルと一致するが、PMS方向は線形ではなかった。
そのため、DX21NativeではOLD/NEW共通で次のPMS深さテーブルを使う。

| PMS | 最大ピッチ変化量 | 状態 |
|---:|---:|---|
| 0 | PMS=5相当 | Vibrato OSC時の実測 |
| 1 | 0.125 semitones | 暫定 |
| 2 | 0.25 semitones | 暫定 |
| 3 | 0.5 semitones | 暫定 |
| 4 | 1.0 semitones | 暫定 |
| 5 | 2.0 semitones | 暫定 |
| 6 | 4.0 semitones | 実測 |
| 7 | 8.0 semitones | 実測 |

計算式:

```text
normalized = PMD / 99
pitchDepthSemitones = normalized * pmsDepthSemitones[PMS]
```

`PMS=7`, `PMD=99` の場合:

```text
99 / 99 * 8.0 = 8 semitones
```

`PMS=7`, `PMD=75` の場合:

```text
75 / 99 * 8.0 = 6.06 semitones
```

`PMS=7`, `PMD=50` の場合:

```text
50 / 99 * 8.0 = 4.04 semitones
```

`PMS=7`, `PMD=25` の場合:

```text
25 / 99 * 8.0 = 2.02 semitones
```

`PMS=6`, `PMD=99` の場合:

```text
99 / 99 * 4.0 = 4 semitones
```

`PMS=5`, `PMD=99` の場合:

```text
99 / 99 * 2.0 = 2.0 semitones
```

### Vibrato OSC

実機では `PMS=0`, `PMD>0` の場合、ピッチLFOは無効ではなく Vibrato OSC として動作する。

観察:

- ピッチLFO波形はTriangle相当として扱う。
- 実効PMSは `PMS=5` 相当になる。
- 現在のPMSテーブルでは、`PMD=99` 時に最大 `2.0 semitones` の振れ幅になる。

### 録音解析: PMD=99, SPEED=20, WAVE=TRIANGLE, PMS 7→0

対象ファイル:

```text
audio/PMD99_PMS7-0_SPEED20_mono.wav
```

内容:

- `PMD=99`
- `SPEED=20`
- `WAVE=TRIANGLE`
- `PMS=7` から `PMS=0` まで順番に録音
- 録音長は約 `61.15秒`
- PMS切り替えは `8秒間隔`
- 最終区間 `PMS=0` はファイル終端の都合で約 `5.15秒`

解析方法:

- 4096 sample window / 512 sample hop で自己相関ベースのF0推定
- 各PMS区間の中央値を基準に semitone 偏差へ変換
- Triangle波形の連続変化を窓解析しているため、極値はやや小さめに出る

解析区間:

| PMS | 区間 |
|---:|---:|
| 7 | 0s ～ 8s |
| 6 | 8s ～ 16s |
| 5 | 16s ～ 24s |
| 4 | 24s ～ 32s |
| 3 | 32s ～ 40s |
| 2 | 40s ～ 48s |
| 1 | 48s ～ 56s |
| 0 | 56s ～ 61.15s |

解析結果:

| PMS | 推定ピッチ変化 p1/p99 | 推定ピーク | 近いテーブル値 |
|---:|---:|---:|---:|
| 7 | -7.19 ～ +7.30 semitones | 約 8.0 | 8.0 |
| 6 | -3.68 ～ +3.57 semitones | 約 4.0 | 4.0 |
| 5 | -0.82 ～ +0.95 semitones | 約 1.0 | 1.0 |
| 4 | -0.45 ～ +0.43 semitones | 約 0.5 | 0.5 |
| 3 | -0.22 ～ +0.21 semitones | 約 0.25 | 0.25 |
| 2 | -0.12 ～ +0.09 semitones | 約 0.125 | 0.125 |
| 1 | -0.05 ～ +0.04 semitones | 約 0.0625 | 0.0625 |
| 0 | -0.94 ～ +0.84 semitones | 約 1.0 | PMS=5相当 |

録音解析のみから読める浅めの候補:

```text
PMS 0: Vibrato OSC, PMS=5相当
PMS 1: 0.0625 semitones
PMS 2: 0.125 semitones
PMS 3: 0.25 semitones
PMS 4: 0.5 semitones
PMS 5: 1.0 semitones
PMS 6: 4.0 semitones
PMS 7: 8.0 semitones
```

ただし、エンジンでは上記の浅め候補ではなく、次の採用テーブルを使う。

```text
PMS 0: Vibrato OSC, PMS=5相当
PMS 1: 0.125 semitones
PMS 2: 0.25 semitones
PMS 3: 0.5 semitones
PMS 4: 1.0 semitones
PMS 5: 2.0 semitones
PMS 6: 4.0 semitones
PMS 7: 8.0 semitones
```

メモ:

- 録音解析値は、採用テーブルより `PMS=1 ～ 5` が1段浅く出ている。
- Triangle波形の連続変化を窓解析しているため、特に小さいPMSでは極値が小さめに推定される可能性がある。
- `PMS=0` は `PMS=5` とほぼ同じ振れ幅で、Vibrato OSCが実効 `PMS=5` 相当という実機観察を支持する。
- `PMS=6` と `PMS=7` は現在の実測値 `4.0`, `8.0` と整合する。

Square LFO は正負に切り替わるため、聴感上の音程変化は次のようになる。

```text
PMD=99: -8 ～ +8 semitones
PMD=75: -6 ～ +6 semitones
PMD=50: -4 ～ +4 semitones
PMD=25: -2 ～ +2 semitones
```

## 未確認項目

- 中間 PMS 値、特に `PMS=1 ～ 4` の最大ピッチ変化量を確認する。
- Mod Wheel はPMD自体を縮小せず、B FUNCTIONの `MW PITCH` 経路としてPMDとは別に加算される構造へ寄せる。
- 現在のDX21Nativeでは暫定的に、Mod Wheel追加分はPMDと同じ深さを使う。

### Mod Wheel / MW PITCH の扱い

説明書のブロック図では、PMD経路と `MW PITCH` 経路は別に描かれている。

DX21Native旧挙動:

```text
pitchLfo = PMD * Delay * (0.35 + ModWheel * 0.65) * LFO
```

この式ではMod Wheelが0のとき、PMD設定値が35%まで縮小される。
ブロック図上はPMDはコントローラーと無関係に常時ピッチへ接続されるため、この挙動は実機構造と合わない。

DX21Native現在の暫定仕様:

```text
basePitch = PMD * Delay
mwPitch   = PMD * ModWheel
pitchLfo  = (basePitch + mwPitch) * LFO
```

今後の改善:

```text
basePitch = PMD * Delay
mwPitch   = MW_PITCH * ModWheel
bcPitch   = BC_PITCH * Breath
pitchLfo  = (basePitch + mwPitch + bcPitch) * LFO + BC_PITCH_BIAS * Breath
```

`MW_PITCH`, `BC_PITCH`, `BC_PITCH_BIAS` はB FUNCTIONパラメータとして別途保持する必要がある。

## LFO アンプリチュードモジュレーション

### AMS / AMD の扱い

説明書より:

- `AMS` は `0 ～ 3` の範囲で、アンプリチュードモジュレーション感度を設定する。
- AMをどのオペレーターにかけるかは、OP1～OP4ごとにON/OFFできる。
- キャリアにかけるとトレモロ効果、モジュレーターにかけるとワウ効果になる。

DX21Nativeでの採用仕様:

| Parameter | 仕様 |
|---|---|
| AMD | AMの深さ。`0 ～ 99` |
| AMS | AM感度。`0 ～ 3` |
| OP別ON/OFF | `ampModEnable` でOPごとに制御 |
| AMD=99, AMS=3 | 最大音量 `100%`、最小音量 `0%` |

計算式:

```text
amDepth = AMD / 99 * AMS / 3
ampFactor = 1 - amDepth * lfoAmplitude
```

`lfoAmplitude` が最大のとき:

```text
AMD=99, AMS=0 -> 最小音量 100%
AMD=99, AMS=1 -> 最小音量 約66.7%
AMD=99, AMS=2 -> 最小音量 約33.3%
AMD=99, AMS=3 -> 最小音量 0%
```

## LFO S/Hold 波形

### 実装精査

DX21Nativeの `S/H` は、Nuked-OPMのnoise LFSR式を使い、S/H更新ごとに新しい値を生成する。
通常波形のように1周期を256分割して読むのではなく、S/HだけはLFO 1周期につき1値を保持する。

生成値:

```text
value = next 8 injected bits from Nuked-OPM noise LFSR
AM = value / 255
PM = value / 127.5 - 1.0
PM *= 2.0  // DX21Native実験設定: S/Hのみピッチ幅2倍
```

値の扱い:

| 項目 | 結果 |
|---|---:|
| value範囲 | 0 ～ 255 |
| AM範囲 | 0.0 ～ 1.0 |
| PM範囲 | -2.0 ～ +2.0 |

注意点:

- `S/H` は固定テーブル参照ではなく、VoiceごとのLFSR状態から更新時に生成する。
- PMは `value=0` を `-1.0`、`value=255` を `+1.0` に正規化する。
- これにより、S/HもSquareと同じく `PMS=7, PMD=99` で `-8 ～ +8 semitones` の範囲を瞬時にランダム移動する。
- 現在のDX21Nativeでは試験的にS/HのみPM出力を2倍し、`PMS=7, PMD=99` で最大 `-16 ～ +16 semitones` 相当まで動くようにしている。

### Nuked-OPMでのS/H仕様

Nuked-OPMでは、`S/H` は固定テーブル参照ではなく、YM2151/OPM内部のLFOロジックをビット単位で再現している。

レジスタ:

| Register | 意味 |
|---|---|
| `0x1b` bits `0..1` | LFO waveform。`3` がS/H |
| `0x18` | LFO周波数 |
| `0x19` bit7 clear | AMD |
| `0x19` bit7 set | PMD |

S/H時の要点:

```text
if lfo_wave == 3 && lfo_clock_lock:
    noise = noise_lfsr & 1
    lfo_bit |= noise
```

- `lfo_wave == 3` のときS/H扱い。
- LFO更新タイミングで `noise_lfsr` の下位bitをLFO値へ注入する。
- 通常波形のような単純なsaw/triangleカウンタではなく、noise LFSR由来のbit列でLFO値が作られる。
- LFO値は内部で `lfo_out1` / `lfo_out2` へ通り、AMD/PMDとの乗算ロジックを経て `lfo_am_lock` / `lfo_pm_lock` にラッチされる。

Nuked-OPMのnoise LFSR:

```text
rst = (noise_lfsr & 0xffff) == 0 && noise_bit == 0
xr = ((noise_lfsr >> 2) & 1) ^ noise_bit
bit = rst | xr
noise_bit = noise_lfsr & 1
noise_lfsr >>= 1
noise_lfsr |= bit << 15
```

DX21Native/WebDX21との差:

| 実装 | S/Hの作り方 |
|---|---|
| Nuked-OPM | LFSR由来のbitをLFO内部値へ注入する動的モデル |
| WebDX21 | Nuked/YM2151風の256値スナップショットテーブル |
| DX21Native旧実装 | WebDX21と同じ256値固定テーブルを1周期内で読む |
| DX21Native現状 | Nuked-OPMのnoise LFSR式からS/H更新ごとに値を生成 |

DX21Native現状の採用方式:

```text
cycle = floor(lfoPhase)
if cycle changed:
    value = nextNukedOpmSampleAndHoldValue()
AM = value / 255
PM = value / 127.5 - 1.0
PM *= 2.0
```

現状判断:

- 実機録音では、Cを押した状態でもS/Hの音程上下がかなり激しく聴こえる。
- そのため、固定スナップショットテーブルではなく、Nuked-OPM由来のLFSR式からS/H更新ごとに新しい値を生成する。
- 完全なNuked-OPM再現には、LFO bit counter、AMD/PMD乗算、`lfo_am_lock` / `lfo_pm_lock` のラッチタイミングまでチップ内部サイクル単位で持ち込む必要がある。
- 現状はDX21Nativeのサンプル単位LFOへ合わせた簡易Nuked-OPM LFSR方式。

### 録音解析: WAVE=S/H, SPEED=27, PMD=99, PMS=7

対象ファイル:

```text
audio/H.wav
```

設定:

| Parameter | Value |
|---|---:|
| WAVE | S/H |
| SPEED | 27 |
| PMD | 99 |
| AMD | 0 |
| PMS | 7 |
| AMS | 0 |

ファイル情報:

| 項目 | 値 |
|---|---:|
| Sample Rate | 48000 Hz |
| Channels | 2 |
| Duration | 約 14.18秒 |

説明書/実装式からのLFO速度:

```text
SPEED=27 -> 約 3.96 Hz
period -> 約 0.252秒
```

解析メモ:

- 自己相関F0推定では、S/Hの急なピッチ段差により一部オクターブ誤検出が出る。
- LFO周期に近い約0.252秒間隔の長めFFT窓で見ると、S/Hらしい階段状のランダムピッチ変化が確認できる。
- 中央値基準では分布の偏りで上下非対称に見えるため、p1/p99の中点を基準に取り直すと、おおむね `±7.3 semitones` 程度まで確認できる。
- PMS=7/PMD=99の理論最大は `±8 semitones` なので、録音解析上は妥当な範囲。

FFT窓解析結果:

```text
median基準 p1/p99: -8.41 ～ +6.29 semitones
p1/p99中点補正後: -7.35 ～ +7.35 semitones
観測最大付近: 約 -7.86 ～ +7.44 semitones
```

現在のNuked-OPM LFSR S/H生成の先頭値:

```text
00: value=  0 -> -16.000 semitones
01: value=  0 -> -16.000 semitones
02: value=128 ->  +0.063 semitones
03: value=  2 -> -15.749 semitones
04: value= 64 ->  -7.969 semitones
05: value=  8 -> -14.996 semitones
06: value= 32 -> -11.984 semitones
07: value= 36 -> -11.482 semitones
08: value=144 ->  +2.071 semitones
09: value=128 ->  +0.063 semitones
```

判断:

- 録音は、`SPEED=27` の周期感と `PMS=7/PMD=99` の大きなランダムピッチ変化を示している。
- 現在の採用方式は、S/H更新ごとにLFSRを進めるため、固定テーブルより実機内部ロジックへ近い。
- さらに精密に合わせるには、録音のS/Hステップ列とLFSRのseed/bit注入タイミングを合わせる必要がある。

### S/H更新速度

S/Hは通常波形と同じように1周期を256分割して読むと、実機より非常に速く変化してしまう。

採用仕様:

```text
通常波形:
  index = floor(frac(lfoPhase) * 256) & 255

S/H:
  index = floor(lfoPhase) & 255
```

つまり、S/HはLFO 1周期につき1つの値を保持する。

例:

```text
SPEED=27 -> 約 3.96Hz
S/H更新周期 -> 約 0.252秒
```

これにより、実機の「ゆっくり段階的に変化する」感覚に近づく。

### 録音解析: WAVE=S/H, SPEED=55, PMD=99, PMS=7

対象ファイル:

```text
audio/H.wav
```

ユーザー記録:

```text
LFO SPEED=55
PMD=99
AMD=0
PMS=7
Cキー
```

ファイル情報:

| 項目 | 値 |
|---|---:|
| Sample Rate | 48000 Hz |
| Channels | 2 |
| Duration | 約 10.26秒 |
| 有効発音区間 | 約 8.94秒 |

説明書準拠のLFO速度:

```text
SPEED=55 -> 約 16.73 Hz
period -> 約 59.8 ms
```

解析メモ:

- 有効発音区間 `8.94秒` に対し、説明書式では約 `149` 個のS/H更新区間になる。
- 録音をこの周期で区切ると、おおむね `149` ステップとして扱える。
- S/Hの更新速度は、説明書準拠の `SPEED=55 -> 約16.7Hz` と整合する。
- FM音色の倍音が強いため、自己相関やFFTピークのF0推定は倍音/オクターブ誤検出を含む。音程振れ幅の絶対値は、録音単体からは慎重に扱う。
