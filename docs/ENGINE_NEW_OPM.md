# DX21Native NEW エンジン仕様書（OPM / Nuked-OPM 準拠系）

## 1. 目的

本書は、DX21Native における **NEW エンジン** の仕様を整理する。

NEW エンジンは、旧 WebDX21 由来の独自 FM エンジンではなく、Yamaha OPM 系チップの挙動、特に Nuked-OPM が重視するチップ内部動作の考え方に準じて、DX21 風 4 Operator FM 音源を再構成するためのエンジンである。

本書の目的は以下である。

1. OLD エンジンとの役割分担を明確にする
2. 現在の C++ 実装に含まれる OPM 風処理を仕様として整理する
3. 今後 Nuked-OPM 準拠度を高める際の設計基準を固定する
4. VST3 / Standalone / Console から共通利用できる DSP コア仕様を明文化する

---

## 2. 用語定義

### 2.1 OLD エンジン

OLD エンジンとは、WebDX21 の JavaScript 実装に由来する独自エンジンを指す。

特徴:

1. DX21 の操作体系を優先する
2. 軽量な 4OP FM 実装である
3. 実機チップのビット一致は目的としない
4. 音色再現は「DX21 風」の近似である

### 2.2 NEW エンジン

NEW エンジンとは、OPM チップの内部挙動に寄せた 4OP FM エンジンを指す。

特徴:

1. OPM 系の phase / EG / feedback / DT1 / TL の考え方を取り入れる
2. Nuked-OPM のようなチップ挙動準拠の実装方針を重視する
3. DX21 の UI パラメータを、OPM 風の内部表現へ変換して発音する
4. 将来的な実機再現精度向上の主対象とする

### 2.3 Nuked-OPM との関係

本プロジェクトにおける NEW エンジンは、Nuked-OPM の設計思想を参照し、OPM チップ内部の挙動に近づけることを目指す。

ただし、現段階では以下を区別する。

1. Nuked-OPM のソースコードをそのまま組み込んだエンジン
2. Nuked-OPM が再現している OPM チップ挙動に準じて設計した自前エンジン

現在の `Source/Engine` 実装は、後者、すなわち **OPM / Nuked-OPM 準拠思想の自前 C++ 実装** として整理する。

---

## 3. 現在の実装対象

現在の NEW エンジン相当の実装は、以下のファイル群に分かれている。

```text
Source/Engine/
├─ Dx21Types.h / .cpp       # Patch, Operator, LFO, Effects, clamp, normalize
├─ Dx21Tables.h / .cpp      # ratio, algorithm, sine, LFO speed, DT1 offset
├─ Dx21Envelope.h / .cpp    # OPM-style EG
├─ Dx21Voice.h / .cpp       # 4OP FM voice, phase, feedback, TL, LFO
├─ Dx21Engine.h / .cpp      # voice管理, render, limiter, effects
└─ Dx21Sysex.h / .cpp       # DX21 VMEM decode
```

現状のビルドターゲットでは、これらは `dx21_engine` static library としてまとめられる。

---

## 4. 設計方針

### 4.1 基本方針

NEW エンジンは、DX21 のパネルパラメータを入力として受け取り、内部では OPM 系チップに近い処理単位へ変換して発音する。

重要な方針は以下である。

1. UI パラメータは DX21 互換を維持する
2. 内部演算は OPM 風に寄せる
3. 旧 JS エンジンとの音色差は許容する
4. 今後の比較対象は OLD ではなく、実機録音および Nuked-OPM 準拠挙動とする
5. Standalone / VST3 / Console から同一エンジンを利用する

### 4.2 非目的

NEW エンジンの初期段階では、以下を非目的とする。

1. DX21 実機との完全ビット一致
2. DAC / アナログ出力段の完全再現
3. ノイズ成分や個体差の再現
4. YM2151 / YM2164 の全レジスタ互換エミュレーション
5. SysEx 送信機能

ただし、将来的な精密化のために、内部構造はチップ準拠へ寄せられるようにする。

---

## 5. パッチ構造

NEW エンジンは、既存の `Dx21Patch` を入力構造として使用する。

```cpp
struct Dx21Patch
{
    int algorithm;
    int feedback;
    int transpose;
    Dx21Lfo lfo;
    Dx21Effects effects;
    std::array<Dx21Operator, 4> operators;
};
```

### 5.1 Global

| 項目 | 範囲 | 内容 |
|---|---:|---|
| algorithm | 1..8 | 4OP 接続アルゴリズム |
| feedback | 0..7 | OP4 への feedback 量 |
| transpose | -24..+24 | 半音単位の移調 |

### 5.2 Operator

| 項目 | 範囲 | 内容 |
|---|---:|---|
| ratioIndex | 0..63 | DX21 ratio table index |
| detune | -3..+3 | OPM DT1 風 detune |
| level | 0..99 | 出力レベル / TL 変換元 |
| rateScale | 0..3 | EG rate key scale |
| levelScale | 0..99 | keyboard level scaling |
| velocity | 0..7 | velocity sensitivity |
| ampModEnable | bool | LFO AM の適用有無 |
| enabled | bool | operator 有効/無効 |

### 5.3 Envelope

| 項目 | 範囲 | 内容 |
|---|---:|---|
| attackRate | 0..31 | Attack rate |
| decay1Rate | 0..31 | Decay1 rate |
| decay1Level | 0..15 | Decay1 level |
| decay2Rate | 0..31 | Decay2 rate |
| releaseRate | 0..15 | Release rate |

### 5.4 LFO

| 項目 | 範囲 | 内容 |
|---|---:|---|
| speed | 0..99 | LFO speed |
| delay | 0..99 | LFO delay |
| pitchDepth | 0..99 | PMD |
| ampDepth | 0..99 | AMD |
| pitchSensitivity | 0..7 | PMS |
| ampSensitivity | 0..3 | AMS |
| sync | bool | note on 時に LFO 位相同期 |
| wave | 0..3 | Saw / Square / Triangle / S/H |

---

## 6. Operator 配列と OPM slot 対応

内部 operator index は 0..3 とする。

| 内部index | 表示名 | OPM slot相当 | 役割例 |
|---:|---|---|---|
| 0 | OP1 | C2 | carrier になりやすい |
| 1 | OP2 | M2 | modulator / carrier |
| 2 | OP3 | C1 | carrier / modulator |
| 3 | OP4 | M1 | feedback 対象 |

feedback は原則として OP4、すなわち index 3 に適用する。

---

## 7. Algorithm 仕様

NEW エンジンでも、DX21 の 4OP / 8 algorithm 構造を使用する。

| Algorithm | 構造 |
|---:|---|
| 1 | `4>3>2>1` |
| 2 | `4+3>2>1` |
| 3 | `3>2>1 + 4>1` |
| 4 | `2>1 + 4>3>1` |
| 5 | `2>1 + 4>3` |
| 6 | `4>(1+2+3)` |
| 7 | `4>3 + 1 + 2` |
| 8 | `1 + 2 + 3 + 4` |

実装上は、carrier 配列と dependency 配列で表現する。

```cpp
struct Algorithm
{
    std::array<int, 4> carriers;
    int carrierCount;
    std::array<std::array<int, 4>, 4> deps;
    std::array<int, 4> depCounts;
};
```

---

## 8. 周波数・位相仕様

### 8.1 基本周波数

基本周波数は MIDI note と transpose から求める。

```text
baseFrequency = midiNoteToFrequency(note + transpose)
```

Pitch Bend と Pitch LFO は半音単位で合成し、周波数比へ変換する。

```text
baseFrequency *= 2^((bendSemitones + appliedPitchLfo) / 12)
```

Pitch Bend の範囲は現状 ±2 semitone とする。

### 8.2 Ratio

operator 周波数は、DX21 ratio table を使用して求める。

```text
operatorFrequency = baseFrequency * ratio + detuneOffset
```

ratio table は 64 要素で、DX21 の ratio index と対応する。

### 8.3 OPM-style phase advance

NEW エンジンでは、連続位相ではなく、OPM 風の有限精度 phase step に寄せる。

```text
phaseSteps = 2^20 = 1048576
increment = round(clamp(frequency, 0, sampleRate * 0.49) * phaseSteps / sampleRate)
phase += increment * 2π / phaseSteps
```

目的:

1. 周波数が連続値のままではなく、チップ内部カウンタ的に丸められる
2. 高域での不自然な発散を抑える
3. OLD エンジンとの差分を明確にする

---

## 9. DT1 / Detune 仕様

DX21 の `detune -3..+3` は、OPM DT1 風の周波数オフセットへ変換する。

現状実装では、OPM DT1 table 相当として以下の整数テーブルを用いる。

```text
{ 16, 17, 19, 20, 22, 24, 27, 29 }
```

変換では以下を考慮する。

1. detune 符号
2. key code
3. block
4. note index
5. OPM base fnum 相当値

この処理により、detune は単純な cent 加算ではなく、key / block に応じた OPM 風のずれとして作用する。

---

## 10. Envelope Generator 仕様

### 10.1 基本モデル

NEW エンジンの EG は、OPM 風の attenuation 管理を採用する。

| 項目 | 内容 |
|---|---|
| 管理量 | attenuation dB |
| 無音側 | 96 dB |
| 最大音量側 | 0 dB |
| 内部 index | 0..1023 |
| dB range | 128 dB |

EG stage は以下である。

```text
Off -> Attack -> Decay1 -> Decay2 -> Release -> Off
```

### 10.2 EG tick

EG は audio sample 単位ではなく、EG tick を内部基準として進む。

```text
EG_TICK_HZ = 3579545 / 64 / 3
```

sample rate に対して remainder を蓄積し、1 tick 以上たまった分だけ EG を進める。

```text
egRemainder += EG_TICK_HZ / sampleRate
while egRemainder >= 1:
    egRemainder -= 1
    advanceEgTick()
```

### 10.3 Rate 変換

Attack / Decay / Release は、DX21 パラメータを OPM 風の実効 rate へ変換する。

| stage | 入力範囲 | scale |
|---|---:|---:|
| Attack | 0..31 | 2.0 |
| Decay1 | 0..31 | 1.85 |
| Decay2 | 0..31 | 1.85 |
| Release | 0..15 | 3.75 + bias 2.0 |

rateScale と note による key scale value を加算し、0..63 に clamp する。

### 10.4 D1L 変換

DX21 の `decay1Level` は、attenuation dB へ変換する。

```text
if decay1Level >= 15:
    target = 0 dB
else:
    target = (15 - decay1Level) * 3 dB
```

---

## 11. Level / TL 仕様

### 11.1 Carrier amplitude

DX21 の `level 0..99` は、carrier 用の振幅へ変換する。

```text
normalized = level / 99
carrierAmp = 10^(-((1 - normalized) * 48) / 20)
```

### 11.2 Modulator index

modulator は carrier と同じ振幅カーブだけではなく、FM index 用の別カーブを混合する。

```text
carrierAmp = outputLevelToCarrierAmplitude(level)
shapedIndex = normalized ^ 2.2
modulatorIndex = carrierAmp * 1.08 + shapedIndex * 0.08
```

### 11.3 OPM-style TL target

NEW エンジンでは、`level 0..99` を OPM TL 風の target unit へ変換する。

```text
TL target = round((1 - level / 99) * 127)
```

内部では TL を substep 付きで追従させる。

```text
TL_SUBSTEPS = 8
TL_RAMP_SECONDS = 0.012
```

これにより、パラメータ変更時の段差を緩和し、チップ的な level 変化に近づける。

---

## 12. Feedback 仕様

feedback は OP4、すなわち内部 index 3 に適用する。

2サンプル分の履歴を保持する。

```text
feedbackInput = feedbackHistory[0] + feedbackHistory[1]
```

feedback level 0..7 は shift 量へ変換し、位相加算量へ反映する。

```text
shift = feedback + 6
feedbackPhase = bus * 2^shift * 2π / (1024 * 65536)
```

OPM 風の operator bus gain を通して、実際の phase modulation 量へ変換する。

---

## 13. Operator bus 仕様

NEW エンジンでは、modulator の出力を直接 radian 加算するのではなく、OPM 風の bus 値として扱う。

主要定数:

```text
OPM_SINE_INDEX_STEPS = 1024
OPM_OPERATOR_BUS_PEAK = 8192
OPM_BUS_PHASE_GAIN = 9.0 / (OPM_OPERATOR_BUS_PEAK * π / OPM_SINE_INDEX_STEPS)
```

operator の出力は以下の2系統を持つ。

| 出力 | 用途 |
|---|---|
| audio | carrier として最終加算される音声信号 |
| modulation | downstream operator へ渡す phase modulation bus |

```cpp
struct OperatorRender
{
    double audio;
    double modulation;
};
```

---

## 14. LFO 仕様

### 14.1 LFO speed

`speed 0..99` は非線形に Hz へ変換する。

```text
if speed == 0:
    hz = 0.01
else:
    hz = 55.0 * (speed / 99)^2.0246997291383155
```

### 14.2 Waveform

| wave | 内容 |
|---:|---|
| 0 | Saw Up |
| 1 | Square |
| 2 | Triangle |
| 3 | Sample & Hold |

AM は 0..1、PM は -1..+1 に clamp する。

### 14.3 Pitch modulation

Pitch LFO は、PMD と PMS から半音単位の変調量を求める。

```text
pitchDepth = PMD / 99
pitchSensitivity = PMS / 7
pitchLfoSemitone = pitchDepth * pitchSensitivity * 8.0 * waveform
```

`pitchSensitivity == 0` の場合は、Vibrato OSC 互換として triangle shape を用いる。

### 14.4 Amp modulation

AM は operator 単位で適用される。

`ampModEnable == false` の operator には AM を適用しない。

AMS 0..3 は正規化し、AMD 0..99 は段階的な depth table へ変換する。

---

## 15. レンダリング手順

1サンプルあたりの処理順は以下とする。

```text
Dx21Engine::renderSample()
  1. globalLfoAge を更新
  2. active voice をすべて render
  3. 非active voice を削除
  4. voice 合算値に output gain を乗算
  5. limiter + declick
  6. effects 処理
  7. stereo sample を返す
```

Voice 内部の処理順は以下である。

```text
Dx21Voice::render()
  1. voice age を更新
  2. algorithm を取得
  3. LFO phase / AM / PM を計算
  4. pitch LFO を1サンプル遅延適用
  5. base frequency を計算
  6. carrier operator を再帰的に render
  7. carrier 数で正規化
  8. 非有限値を検出した場合は failed 扱い
```

---

## 16. Limiter / Declick

Engine 出力には、最終段で soft limiter と declick を適用する。

```text
limited = tanh(sample * 0.92) * 0.98
delta = clamp(limited - lastOutput, -0.42, +0.42)
lastOutput += delta
```

目的:

1. feedback や高 level 設定での過大出力を抑える
2. note on / patch change 時のクリックを緩和する
3. Console / Standalone / VST3 で共通の安全出力を得る

---

## 17. Effects 仕様

NEW エンジンの現行 C++ 実装では、Engine 内に簡易 effects chain を持つ。

対象パラメータ:

| 項目 | 範囲 | 内容 |
|---|---:|---|
| reverb | 0..99 | 簡易 reverb feedback / wet 量 |
| mix | 0..99 | dry / wet balance |
| tone | 0..99 | low-pass cutoff |
| chorus | 0..99 | chorus depth / rate |
| delay | 0..99 | delay time |

注意:

1. これは DX21 実機内蔵 effect の再現ではない
2. WebAudio 版の外部 effects とも完全一致しない
3. Standalone / VST3 で扱いやすいよう、Engine 内に統合された補助 effect として扱う

将来的に実機再現を優先する場合、effects は Engine core から分離し、Post FX 層へ移動することを検討する。

---

## 18. Voice 管理

`Dx21Engine` は polyphonic voice pool を管理する。

| 操作 | 内容 |
|---|---|
| noteOn | 同一 note の既存 voice を削除して新規 voice を追加 |
| noteOff | 該当 note の envelope を release へ移行 |
| panic | 全 voice と内部履歴をリセット |
| maxVoices | 1..32 に clamp |

現行既定値は 8 voices とする。

---

## 19. SysEx / VMEM との関係

NEW エンジンは、DX21 の 32 voice bulk VMEM を `Dx21Patch` へ decode して使用する。

VMEM 仕様:

| 項目 | 値 |
|---|---:|
| voice count | 32 |
| voice size | 128 bytes |
| data offset | 6 |
| data size | 4096 bytes |
| minimum file size | 4104 bytes |

operator block の VMEM order は以下である。

```text
OP4, OP2, OP3, OP1
```

内部 index への変換は以下である。

```text
[3, 1, 2, 0]
```

decode 後の patch は NEW エンジンの OPM 風内部処理へ渡される。

---

## 20. OLD エンジンとの差分

| 項目 | OLD | NEW |
|---|---|---|
| 基本方針 | WebDX21 JS 由来の独自実装 | OPM / Nuked-OPM 準拠思想 |
| 目的 | DX21 風に軽く鳴らす | チップ挙動へ近づける |
| phase | 連続的な近似 | OPM-style 2^20 phase step |
| EG | JS 近似 | OPM-style EG tick / rate table |
| detune | 独自近似 | OPM DT1 風 offset |
| feedback | 独自FM feedback | OP4 bus履歴 + OPM-style shift |
| TL | level 0..99中心 | TL target 0..127 + substep ramp |
| LFO | DX21風近似 | OPM風 depth / delay を含む再設計 |
| 位置づけ | 旧互換 / 比較用 | 今後の主エンジン |

---

## 21. テスト方針

NEW エンジンでは、以下のテストを整備する。

### 21.1 単体テスト

1. ratio table size / value
2. algorithm carrier / dependency
3. patch normalize range
4. EG stage transition
5. fast attack timing
6. sysex decode
7. VMEM operator order

### 21.2 レンダリングテスト

1. 単一 carrier patch の peak / RMS
2. feedback patch の finite check
3. 全 algorithm の note render
4. LFO PMD / AMD の範囲確認
5. release 後の voice 消滅確認

### 21.3 比較テスト

今後追加すべき比較対象:

1. OLD エンジンとの差分レンダリング
2. 実機 DX21 録音との envelope / pitch / spectrum 比較
3. Nuked-OPM 相当処理との phase / EG / feedback 比較

---

## 22. 今後の設計課題

### 22.1 Engine 分離

現在は `Dx21Engine` に NEW 相当の処理が集約されている。

将来的には以下のような分離を検討する。

```text
IEngine
├─ OldDx21Engine
└─ NewOpmDx21Engine
```

または、Voice / Envelope / Tables のみを切り替える方式も考えられる。

```text
Dx21Engine
├─ OldVoiceModel
└─ NewOpmVoiceModel
```

### 22.2 RT-safe 化

VST3 化に向けて、audio thread 内の mutex lock を避ける必要がある。

推奨方針:

1. UI thread で patch snapshot を作成
2. audio thread へ lock-free に受け渡す
3. audio callback 内で動的確保しない
4. parameter smoothing を audio thread 内で完結させる

### 22.3 Nuked-OPM 準拠度の明確化

今後、以下を明確にする必要がある。

1. Nuked-OPM ソースを直接利用するのか
2. Nuked-OPM の挙動を参照した自前実装を続けるのか
3. YM2151 / YM2164 のどちらを主な参照対象とするのか
4. DX21 の 4OP パラメータを OPM register 相当にどう写像するのか

### 22.4 Post FX 分離

現在の reverb / delay / chorus / tone は Engine 内にある。

将来的には以下に分けることを検討する。

```text
Core FM Engine
  ↓
Output Safety: limiter / declick
  ↓
Post FX: tone / chorus / delay / reverb
```

これにより、実機再現モードと演奏用拡張モードを切り替えやすくなる。

---

## 23. 受け入れ条件

NEW エンジン仕様として、以下を満たすことを目標とする。

1. DX21 VMEM から読み込んだ 32 voice が発音可能
2. 8 algorithm がすべて安定して動作する
3. EG / LFO / feedback / DT1 が OPM 風処理で説明可能である
4. NaN / Inf が出力されない
5. OLD エンジンとの差分が文書上説明されている
6. Console / Standalone / VST3 で同一 DSP core を利用できる
7. 将来的に Nuked-OPM との比較テストを追加できる構造である

---

## 24. 関連ドキュメント

1. `WebDX21_JS_Engine_Spec.md`  
   OLD エンジン / WebDX21 JS 由来仕様

2. `DX21_Native_VST3_移植仕様書.md`  
   Native / VST3 全体仕様

3. `DX21_Native_VST3_実装チケット分解.md`  
   実装フェーズとチケット分解

4. `WebDX21_DX21_SYX_Import_Spec.md`  
   DX21 SysEx / VMEM 取り込み仕様

5. `WebDX21_UI_Spec.md`  
   UI レイアウト・操作仕様
