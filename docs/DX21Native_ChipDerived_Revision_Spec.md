# DX21Native チップ寄せ修正仕様書

## 目的

DX21Native は、現時点で DX21 音色の再現がある程度できている。今後は実測ではなく、公開されている資料と Nuked-OPM のチップ由来実装から推測できる範囲で、エンジンをより YM2151 / YM2164 / OPP 的な構造へ寄せる。

本仕様書の目的は、現行 DX21Native の音源エンジンを大きく壊さずに、段階的にチップ寄りへ修正するための方針を整理することである。

---

## 前提

### Nuked-OPM

Nuked-OPM は YM2151 / YM2164 系 OPM チップの低レベルエミュレーションである。

特徴:

- レジスタ、スロット、チャンネル、内部クロック単位で処理する
- EG / PG / LFO / Operator / Mixer / Timer / Noise などの内部状態を持つ
- YM2151 および YM2164 の decap / die shot 情報を参照している
- TL は音量ではなく減衰量として扱う
- 通常 YM2151 では `TL << 3` が EG 出力へ加算される
- YM2164 / OPP では TL Ramp により `opp_tl` が 1/8 TL 単位で変化する

### DX21Native

DX21Native は DX21 音色パラメータを C++/JUCE 上で鳴らすための推測実装である。

特徴:

- 4 Operator FM をサンプル単位で直接計算する
- DX21 の LEVEL 0–99 を直接音量・変調量へ変換している
- 現在は Carrier と Modulator で別々の推測カーブを使っている
- OPP TL Ramp 風の平滑化処理を持つ
- 実チップ完全一致ではなく、実用的な DX21 風エンジンである

---

## 基本方針

現行エンジンを全面的に Nuked-OPM 化するのではなく、音色差に効く部分から順にチップ寄りへ変更する。

優先順位:

1. LEVEL を直接振幅へ変換せず、まず OPM TL 相当値へ変換する
2. TL を減衰量として扱い、dB / attenuation ベースで統合する
3. Carrier と Modulator の LEVEL カーブを同じ TL モデルから派生させる
4. Modulator は OPM operator bus への出力として扱う
5. EG は可能な限り振幅倍率ではなく減衰量として統合する
6. LFO PMS / PMD は OPM 的な段階テーブルへ寄せる
7. Pitch EG は OPM には存在しないため、DX21 側の高レベル機能として別実装する

---

# 1. LEVEL → TL モデル

## 1.1 現行 DX21Native

現行実装では、LEVEL 0–99 から OPP / YM2151 TL 相当値へ以下で変換している。

```cpp
TL_target = round((1.0 - clamp(level, 0, 99) / 99.0) * 127.0);
```

意味:

```text
DX21 LEVEL 99 -> TL 0
DX21 LEVEL 0  -> TL 127
```

これは方向としては正しい。

DX21 LEVEL は大きいほど出力が大きい。
YM2151 / OPP TL は小さいほど出力が大きい。

したがって LEVEL と TL は逆方向である。

---

## 1.2 推奨: TL を中間表現として正式採用する

今後は、LEVEL を直接 `carrierAmp` や `modulatorIndex` に変換せず、必ず一度 TL 相当値へ変換する。

```cpp
inline double dx21LevelToOpmTl(double level)
{
    level = clampDouble(level, 0.0, 99.0);
    return std::round((1.0 - level / 99.0) * 127.0);
}
```

TL は音量ではなく減衰量である。

```text
TL 小さい -> 出力大
TL 大きい -> 出力小
```

---

## 1.3 TL → dB

YM2151 / OPM 系では、TL 1 step を約 0.75 dB の減衰として扱う。

```cpp
inline double opmTlToDb(double tl)
{
    return clampDouble(tl, 0.0, 127.0) * 0.75;
}
```

---

## 1.4 TL → 振幅

```cpp
inline double dbToAmplitude(double db)
{
    return std::pow(10.0, -db / 20.0);
}

inline double opmTlToAmplitude(double tl)
{
    return dbToAmplitude(opmTlToDb(tl));
}
```

したがって、Chip-like な LEVEL → Carrier 振幅は以下。

```cpp
inline double dx21LevelToCarrierAmplitudeChipLike(double level)
{
    return opmTlToAmplitude(dx21LevelToOpmTl(level));
}
```

---

## 1.5 注意: Full 127 TL は変化が大きい

`LEVEL 0..99 -> TL 0..127` とすると、最大減衰は

```text
127 * 0.75 = 95.25 dB
```

になる。

現行 DX21Native の Carrier レベルは 48 dB レンジである。

```cpp
carrierAmp = pow(10.0, -((1.0 - level/99.0) * 48.0) / 20.0);
```

したがって、単純に 95.25 dB レンジへ変更すると、中低 LEVEL が大きく小さくなり、音が細くなる可能性がある。

そのため、実装では以下の3モードを用意する。

```cpp
enum class LevelModel
{
    CurrentDx21Native,  // 現行48dBカーブ
    ChipLikeOpm127,    // TL 0..127 / 0.75dB
    Hybrid             // 現行とChipLikeのブレンド
};
```

推奨初期値は `Hybrid` である。

---

# 2. Hybrid LEVEL モデル

## 2.1 目的

いきなり完全 Chip-like にすると音色が大きく変わるため、現行音色を維持しながらチップ寄りへ移行する。

## 2.2 式

```cpp
double outputLevelToCarrierAmplitudeHybrid(double level, double chipAmount)
{
    const double oldAmp = outputLevelToCarrierAmplitude(level);
    const double chipAmp = dx21LevelToCarrierAmplitudeChipLike(level);

    chipAmount = clampDouble(chipAmount, 0.0, 1.0);

    // 振幅を線形ブレンドすると中間域が不自然になりやすい。
    // dBでブレンドする。
    const double oldDb = -20.0 * std::log10(std::max(oldAmp, 1.0e-9));
    const double chipDb = -20.0 * std::log10(std::max(chipAmp, 1.0e-9));
    const double db = oldDb * (1.0 - chipAmount) + chipDb * chipAmount;

    return std::pow(10.0, -db / 20.0);
}
```

初期値:

```cpp
constexpr double kChipLevelBlend = 0.50;
```

段階的に以下のように上げる。

```text
0.25 -> 0.50 -> 0.75 -> 1.00
```

---

# 3. OPP TL Ramp の扱い

## 3.1 Nuked-OPM の OPP TL Ramp

Nuked-OPM の YM2164 / OPP TL Ramp は、TL レジスタ bit7 により有効化される。

```text
bit7 = 0:
    TL を即時反映
    internal_TL = TL << 3

bit7 = 1:
    bit0-6 を target_TL として扱う
    internal_TL を target_TL へ 1/8 TL 単位で近づける
```

内部値 `opp_tl` は 1 TL を 8 substeps に分けた値である。

---

## 3.2 DX21Native の現行 Ramp

DX21Native にはすでに OPP TL Ramp 風の処理がある。

```cpp
stepInterval = max(1.0, sampleRate * 0.012 / (127 * 8));

if (floor(current / 8) < target)
    current += 1.0;
else if (floor(current / 8) > target)
    current -= 1.0;
else
    current = target * 8;
```

この構造はチップ寄せ方針と矛盾しない。

---

## 3.3 修正方針

Ramp は音色本体ではなく、LEVEL 変更時の平滑化として扱う。

つまり、以下の流れを正式仕様にする。

```text
DX21 LEVEL
    ↓
TL_target = levelToOppTlTarget(LEVEL)
    ↓
TL_units = TL_target * 8
    ↓
OPP TL Ramp 風に平滑化
    ↓
TL_smoothed
    ↓
TL_smoothed を減衰量として使用
```

現行 DX21Native は Ramp 後に LEVEL へ戻している。

```cpp
LEVEL_smoothed = (1.0 - TL_units / (127.0 * 8.0)) * 99.0;
```

チップ寄せでは、最終的には LEVEL に戻さず、TL / dB のまま使う方が自然である。

---

## 3.4 移行案

### Phase 1: 現状維持

```text
LEVEL -> TL -> Ramp -> LEVEL -> amplitude
```

### Phase 2: 推奨

```text
LEVEL -> TL -> Ramp -> dB -> amplitude
```

### Phase 3: 完全整理

```text
LEVEL -> TL
Keyboard Scaling -> TL offset
Velocity -> TL or dB offset
EG -> dB attenuation
AM -> dB attenuation offset
合算 -> amplitude
```

---

# 4. Carrier 音量

## 4.1 現行式

```cpp
normalized = clamp(level, 0, 99) / 99.0;
carrierAmp = pow(10.0, -((1.0 - normalized) * 48.0) / 20.0);
```

## 4.2 Chip-like 式

```cpp
tl = dx21LevelToOpmTl(level);
carrierAmp = pow(10.0, -(tl * 0.75) / 20.0);
```

## 4.3 推奨実装

```cpp
double outputLevelToCarrierAmplitude2(double level)
{
    switch (levelModel)
    {
    case LevelModel::CurrentDx21Native:
        return outputLevelToCarrierAmplitude(level);

    case LevelModel::ChipLikeOpm127:
        return dx21LevelToCarrierAmplitudeChipLike(level);

    case LevelModel::Hybrid:
    default:
        return outputLevelToCarrierAmplitudeHybrid(level, kChipLevelBlend);
    }
}
```

---

# 5. Modulator Index

## 5.1 現行式

DX21Native は、Modulator LEVEL を Carrier と別カーブで FM index 化している。

```cpp
carrierAmp = outputLevelToCarrierAmplitude(level);
normalized = clamp(level, 0, 99) / 99.0;
shapedIndex = pow(normalized, 2.2);

modulatorIndex = carrierAmp * 1.08 + shapedIndex * 0.08;
```

この式は音作りとしては有効だが、チップの考え方とは少し異なる。

---

## 5.2 チップ寄せの考え方

OPM では Modulator も Carrier と同じ Operator 出力である。

違いは、Carrier 出力は音声として mixer へ行き、Modulator 出力は次段 Operator の位相入力へ入ることである。

したがって、Modulator LEVEL もまず TL 減衰モデルで出力を作る。

DX21Native の現在の bus 設計では、

```cpp
value.modulation = wave * modulatorIndex * kOpmOperatorBusPeak;
```

で bus 値を作り、後段で `opmPhaseBusToRadians()` により位相ラジアンへ変換している。

`kOpmOperatorBusPeak = 8192` のとき、bus peak はおよそ 9 rad 相当になる設計である。

したがって、Chip-like モデルでは `modulatorIndex` は基本的に Carrier と同じ振幅スケールでよい。

---

## 5.3 推奨式

```cpp
inline double outputLevelToModulatorIndexChipLike(double level)
{
    const double amp = dx21LevelToCarrierAmplitudeChipLike(level);
    return amp * kChipModulatorIndexScale;
}
```

初期値:

```cpp
constexpr double kChipModulatorIndexScale = 1.00;
```

現行式の LEVEL 99 は約 1.16 になるため、音が暗くなりすぎる場合は以下を試す。

```cpp
constexpr double kChipModulatorIndexScale = 1.08;
// または 1.16
```

---

## 5.4 Hybrid Modulator

```cpp
double outputLevelToModulatorIndexHybrid(double level, double chipAmount)
{
    const double oldIndex = outputLevelToModulatorIndex(level);
    const double chipIndex = outputLevelToModulatorIndexChipLike(level);

    chipAmount = clampDouble(chipAmount, 0.0, 1.0);
    return oldIndex * (1.0 - chipAmount) + chipIndex * chipAmount;
}
```

推奨初期値:

```cpp
constexpr double kChipModulatorBlend = 0.50;
```

ただし Nuked-OPM と比較して NEW Engine が実機よりまろやかに聞こえる場合、`chipIndex` 側へ寄せすぎると中間 LEVEL の Modulator が弱くなりやすい。
DX21Native の現行 `oldIndex` は中間 LEVEL で倍音を作りやすいため、耳合わせでは以下を暫定値にする。

```cpp
constexpr double kChipModulatorBlend = 0.25;
```

また、NEW Engine では Modulator のアタック丸めを外す。
Nuked-OPM は Operator 出力をそのまま次段位相へ入れるため、3ms 程度のソフトニングが残るとアタック成分が実機より丸くなりやすい。

DX21Native の bus peak は約 9 rad 相当だが、Nuked-OPM の `op_phase = phase + mod` と比較すると通常の位相変調量が弱く聞こえる。
NEW Engine の暫定耳合わせ値として、通常の phase modulation / feedback phase conversion に以下の倍率を掛ける。

```cpp
constexpr double kChipPhaseModGain = 1.6;
```

次に音へ出やすい差分として、NEW Engine の Operator core だけ以下を適用する。

- Operator phase を 1024 step 相当に丸めてから波形参照する
- Operator 出力を 14bit signed 相当に量子化する
- Modulator bus も整数 bus 相当に丸める
- NEW Engine では、波形値へ amplitude を単純乗算するのではなく、Nuked-OPM の `logsinrom` 実テーブルから quarter-wave 256 step の log-sine 減衰を作り、LEVEL/EG/Velocity/AM 由来の amplitude 減衰と足してから log attenuation step に丸める
- 丸めた attenuation は Nuked-OPM の `exprom` 実テーブルと `op_pow = op_atten >> 8` 相当で出力へ戻し、最後に 14bit signed 相当へ丸める

注意: 8bit mantissa + exponent 風の簡易近似は、attenuation step と exponent shift の単位を誤ると decay 中に大きな段差や音量の戻りを作る。
`exprom` は `op_atten` の整数単位と `op_pow = op_atten >> 8` の関係をそのまま扱い、dB step 近似と混ぜない。

Nuked-OPM は `logsinrom` / `exprom` / bit shift で Operator 出力を作るため、連続 `std::sin()` のままよりアタックや高域の質感が近づきやすい。
現在の NEW Engine は `logsinrom` / `exprom` の両方を実テーブル化している。

Phase modulation も NEW Engine では、最終的な radians phase に modulation を足してから 1024 step 化するのではなく、以下の順にする。

```text
op_phase = (basePhaseIndex + modulationIndex + feedbackIndex) & 1023
```

これは Nuked-OPM の `op_phase = (op_phase_in + op_mod_in) & 1023` に合わせるためである。

複数 Modulator が同じ Operator へ入る場合、現行 DX21Native は依存 Operator の bus を単純加算している。
Nuked-OPM では algorithm table の `mod1` / `mod2` を経由し、最後に `(mod1 + mod2) >> 1` で合流する。
NEW Engine では最小変更として、複数入力時のみ modulation bus を 2 入力相当で平均化し、整数 bus に丸める。
これにより、複数 Modulator 合流時の過剰な位相入力を抑え、チップ的な合流スケールに近づける。

Carrier 合算後の voice output も、NEW Engine では 14bit signed 相当に丸める。
Operator 内部を段階化しても、最終 sum が完全な浮動小数のままだとチップ由来の粗さが薄まるため、voice mixer 段でも最小限の量子化を入れる。

---

# 6. EG 統合

## 6.1 現行

現行 DX21Native は、EG を振幅倍率として使っている。

```cpp
envelopeAmp = envelopes[op].next();

carrierAmp = outputLevelToCarrierAmplitude(level) * envelopeAmp;
modulatorIndex = outputLevelToModulatorIndex(level) * envelopeAmp;
```

## 6.2 チップ寄せ方針

チップ的には、EG と TL はともに減衰量であり、加算される。

```text
totalAttenuation = TL + EG
```

dB モデルでは以下。

```cpp
levelDb = opmTlToDb(tl);
egDb = envelopeAttenuationDb;
totalDb = levelDb + egDb;
amp = pow(10.0, -totalDb / 20.0);
```

---

## 6.3 移行案

既存 `envelope.next()` が振幅を返す場合、まずは変換で対応する。

```cpp
double ampToDb(double amp)
{
    return -20.0 * std::log10(std::max(amp, 1.0e-9));
}

const double envAmp = envelopes[op].next();
const double envDb = ampToDb(envAmp);
const double levelDb = opmTlToDb(dx21LevelToOpmTl(level));
const double totalDb = levelDb + envDb;
const double amp = dbToAmplitude(totalDb);
```

ただし、数学的には `levelAmp * envAmp` と近い結果になる。
重要なのは、今後の Keyboard Scaling / Velocity / AM を dB / TL offset として統合しやすくすることである。

NEW Engine の暫定実装では、EG 本体の clock/state machine 全置換の前段として、`envelopeAmp` を 10bit EG attenuation 相当に丸める。

```cpp
egIndex = round(envelopeDb / 128.0 * 1023.0);
quantizedEnvelopeDb = egIndex * 128.0 / 1023.0;
```

これにより、既存の DX21 EG カーブを保ちながら、Nuked-OPM の `eg_out` に近い 10bit 段階感を NEW だけへ加える。
完全な EG 寄せでは、次段階として Nuked-OPM の `eg_inc` / `eg_clock` / attack linearity を移植する。

NEW Engine では次段階として `Dx21ChipEnvelope` を追加し、OLD の `Dx21Envelope` と分離する。
`Dx21ChipEnvelope` は envelope level を 10bit index として保持し、`eg_inc` table による attack / decay / release の進行を行う。
Attack は `egLevel -= egLevel * inc / 16` の非線形進行、Decay/Release は 10bit index への線形加算とする。
これにより、既存 EG を 10bit に丸めるだけの段階から、NEW 専用の EG state machine へ移行する。

---

## 6.4 将来案

`Dx21Envelope` に以下を追加する。

```cpp
double Dx21Envelope::nextAttenuationDb();
```

または、内部で保持している attenuation dB を直接返す。

---

# 7. Keyboard Scaling

## 7.1 現行

```cpp
highKeyAmount = clamp((note - 60) / 36, 0, 1);
scaledLevel = level - highKeyAmount * levelScale * 0.45;
```

LEVEL を減らすことで、高音域の出力を下げている。

## 7.2 チップ寄せ案

LEVEL ではなく TL offset として扱う。

現在の式を TL に換算すると、

```text
level低下量 = highKeyAmount * levelScale * 0.45
TL増加量 ≒ level低下量 * 127 / 99
```

したがって、初期式は以下。

```cpp
double keyboardScaleTlOffset(int note, int levelScale)
{
    const double highKeyAmount = clampDouble((static_cast<double>(note) - 60.0) / 36.0, 0.0, 1.0);
    return highKeyAmount * clampDouble(levelScale, 0.0, 99.0) * 0.45 * 127.0 / 99.0;
}
```

使用例:

```cpp
tl = dx21LevelToOpmTl(level);
tl += keyboardScaleTlOffset(midiNote, op.levelScale);
tl = clampDouble(tl, 0.0, 127.0);
```

---

# 8. Velocity Sensitivity

## 8.1 現行

```cpp
velocityBoost = 1.0 + op.velocity * 0.08 * (noteVelocity / 127.0 - 0.5);
```

これは振幅倍率として処理している。

## 8.2 チップ寄せ案

Velocity は TL offset または dB offset として扱う。

```cpp
double velocityDbOffset(int velocitySensitivity, int noteVelocity)
{
    const double centered = static_cast<double>(noteVelocity) / 127.0 - 0.5;
    const double sens = clampDouble(static_cast<double>(velocitySensitivity), 0.0, 7.0);

    // 初期推定値。現行の 0.08 振幅変化を dB 相当に移す。
    return -centered * sens * 1.2;
}
```

- noteVelocity が高いほど dB offset は負になる
- dB offset が負になると減衰が減り、音が大きくなる

使用例:

```cpp
totalDb = levelDb + envDb + velocityDbOffset(op.velocity, noteVelocity);
```

まずは現行 `velocityBoost` のままでもよい。
LEVEL / Modulator の修正より優先度は低い。

---

# 9. AM / AMS

## 9.1 現行

```cpp
ampMod = op.ampModEnable ? operatorAmpModFactor(ampDepth, lfoAm) : 1.0;
carrierAmp *= ampMod;
modulatorIndex *= ampMod;
```

## 9.2 チップ寄せ案

YM2151 / OPM では AM は減衰量の変調として扱う方が自然である。

現行の振幅倍率ではなく、dB offset として適用する。

```cpp
double ampModDbOffset(double ampDepth, double lfoAm)
{
    // ampDepth: 0..1
    // lfoAm: 0..1
    constexpr double kMaxAmpModDb = 24.0; // 初期推定
    return ampDepth * lfoAm * kMaxAmpModDb;
}
```

使用例:

```cpp
if (op.ampModEnable)
    totalDb += ampModDbOffset(ampDepth, lfoAm);
```

注意:

- AM は音量を下げる方向の変調として扱う
- `lfoAm = 0` で変化なし
- `lfoAm = 1` で最大減衰

この修正は音色への影響が大きいため、LEVEL / Modulator 変更後に行う。

---

# 10. LFO Pitch / PMS / PMD

## 10.1 現行

```cpp
normalized = pitchDepth / 99.0;
normalizedSensitivity = pitchSensitivity / 7.0;
semitoneDepth = normalized * normalizedSensitivity * 8.0;
```

## 10.2 OPM寄せ案

YM2151 / OPM 的な PMS は段階式である。

PMS 0–7 のおおよそのピッチ幅:

```text
PMS 0: VIBRATO OSC  = PMS 5 相当の ±1.00 semitone
PMS 1: ±5 cent      = ±0.05 semitone
PMS 2: ±10 cent     = ±0.10 semitone
PMS 3: ±20 cent     = ±0.20 semitone
PMS 4: ±50 cent     = ±0.50 semitone
PMS 5: ±100 cent    = ±1.00 semitone
PMS 6: ±400 cent    = ±4.00 semitone
PMS 7: ±700 cent    = ±7.00 semitone
```

DX21 実機仕様では、PMS=0 は「ピッチ変調なし」ではない。
PMS=0 のときは LFO WAVE で選択した波形ではなく、VIBRATO OSC（三角波）によるビブラートがかかる。
したがって PMS=0 は depth 0 ではなく、PMS=5 と同じ上下幅として扱う。

また実機確認上、SQUARE 波形かつ PMS=7 の場合だけは、PMD=99 で ±8.00 semitone まで届く。
PMS=6 は ±4.00 semitone のままでよい。

実装:

```cpp
static constexpr double kOpmPmsDepthSemitone[8] = {
    1.00, // PMS=0: VIBRATO OSC（三角波）で PMS=5 相当
    0.05,
    0.10,
    0.20,
    0.50,
    1.00,
    4.00,
    7.00
};

double opmStylePitchModDepth2(int depth, int sensitivity)
{
    const double normalizedDepth = clampDouble(static_cast<double>(depth), 0.0, 99.0) / 99.0;
    const int pms = clampInt(sensitivity, 0, 7);
    return normalizedDepth * kOpmPmsDepthSemitone[pms];
}
```

使用例:

```cpp
const double pitchLfo = opmStylePitchModDepth2(patch.lfo.pitchDepth, patch.lfo.pitchSensitivity)
                      * delay
                      * pitchLfoShape.second;
```

PMD/PMS だけで指定された上下幅へ届く必要があるため、ここでは `modWheel` を深さ倍率として掛けない。
`modWheel` をピッチLFOへどう足すかは、別のコントローラ仕様として扱う。

波形側の注意:

- TRIANGLE は 1/4 周期で +1.0、3/4 周期で -1.0 に届くようにする。
- SAW UP は 8bit step 上で `index 127 = +1.0`、`index 128 = -1.0` へ届くようにする。
- SQUARE は ±1.0 を直接出す。
- PMS=0 は LFO WAVE ではなく VIBRATO OSC（三角波）を使う。
- SQUARE + PMS=7 のときだけ ±8.00 semitone を許す。

---

# 11. Feedback

## 11.1 現行

DX21Native では OP4 に feedback をかけている。

```cpp
feedback = opIndex == 3
    ? opmFeedbackBusToRadians(feedbackHistory[0] + feedbackHistory[1], patch.feedback)
    : 0.0;
```

`opmFeedbackBusToRadians()` では、feedback 値に応じて shift を変えている。

```cpp
shift = feedback + 6;
```

これは OPM 風の考え方に近いため、優先して変更する必要は低い。

---

## 11.2 修正案

最終的には feedback gain をテーブル化する。

当初は以下のような `0..1` の直感的なテーブルを候補にした。

```cpp
static constexpr double kFeedbackGainTable[8] = {
    0.0,
    1.0 / 64.0,
    1.0 / 32.0,
    1.0 / 16.0,
    1.0 / 8.0,
    1.0 / 4.0,
    1.0 / 2.0,
    1.0
};
```

しかし、DX21Native の `feedbackHistory` / `kOpmOperatorBusPeak` / `opmPhaseBusToRadians()` の bus 設計へこの値をそのまま入れると、従来 shift 式より約4倍強くなり、再現性が下がる。

Nuked-OPM の実装では feedback は以下のように処理される。

```c
if (fb == 0)
    mod = 0;
else
    mod = mod >> (9 - fb);
```

したがって `opmPhaseBusToRadians(bus)` に対する倍率としては、以下のテーブルが近い。

```cpp
static constexpr double kFeedbackGainTable[8] = {
    0.0,
    1.0 / 256.0,
    1.0 / 128.0,
    1.0 / 64.0,
    1.0 / 32.0,
    1.0 / 16.0,
    1.0 / 8.0,
    1.0 / 4.0
};
```

つまり、Feedback table 化する場合は `0..1` テーブルではなく、Nuked-OPM の `mod >> (9 - fb)` に相当するスケールへ換算する。
OLD には従来 shift 式を残し、NEW だけこの table を使うと比較しやすい。

NEW Engine では feedback history の入力も `history[0] + history[1]` の単純加算ではなく、`(history[0] + history[1]) / 2` 相当で平均化してから table へ入れる。
Nuked-OPM は modulation 合流段で `(mod1 + mod2) >> 1` を使うため、feedback も同じ bus scale へ揃える。

---

# 12. Pitch EG

## 12.1 注意

YM2151 / YM2164 / OPP には、DX21 のような Pitch Envelope Generator は存在しない。

DX21 の Pitch EG は以下の高レベル構造である。

```text
Key On:
    PL3 -> PL1 : PR1
    PL1 -> PL2 : PR2
    PL2 を保持

Key Off:
    current pitch -> PL3 : PR3
```

したがって、Pitch EG は Nuked-OPM から直接導入できない。
DX21Native 側で独自実装する。

---

## 12.2 PL → cent

```cpp
double pitchEgLevelToCents(int pl)
{
    pl = clampInt(pl, 0, 99);
    if (pl >= 50)
        return (static_cast<double>(pl) - 50.0) * 4800.0 / 49.0;
    return (static_cast<double>(pl) - 50.0) * 4800.0 / 50.0;
}
```

## 12.3 PR → time 推定

公式テーブルがないため、指数時間テーブルで仮実装する。

```cpp
double pitchEgRateToSeconds(int pr)
{
    pr = clampInt(pr, 0, 99);
    constexpr double minTime = 0.005;
    constexpr double maxTime = 8.0;
    return minTime * std::pow(maxTime / minTime, (99.0 - pr) / 99.0);
}
```

Pitch EG は LEVEL / EG / LFO 修正後に実装する。

---

# 13. 推奨実装順

## Phase 1: 安全な構造整理

- `dx21LevelToOpmTl()` を追加
- `opmTlToDb()` を追加
- `opmTlToAmplitude()` を追加
- LevelModel を追加
- Current / ChipLike / Hybrid を切り替え可能にする

## Phase 2: Carrier LEVEL の Chip-like 化

- Carrier amplitude を Hybrid へ変更
- `kChipLevelBlend = 0.25` から開始
- 音が細くなりすぎなければ `0.50` へ上げる

## Phase 3: Modulator LEVEL の Chip-like 化

- `outputLevelToModulatorIndexChipLike()` を追加
- `kChipModulatorIndexScale = 1.00` から開始
- 暗くなりすぎる場合 `1.08` または `1.16` を試す
- `kChipModulatorBlend = 0.50` から開始

## Phase 4: TL Ramp の出力を LEVEL ではなく TL / dB へ移行

- 現行 `nextOperatorLevel()` を維持しつつ、将来的に `nextOperatorTl()` を追加

```cpp
double Dx21Voice::nextOperatorTl(int index, int targetLevel);
```

- 返り値を `TL_smoothed` にする
- `LEVEL_smoothed` へ戻す処理を廃止可能にする

## Phase 5: EG dB 統合

- `envelope.nextAttenuationDb()` を追加
- `levelDb + egDb + velocityDb + ampModDb` で合算

## Phase 6: LFO PMS テーブル化

- `opmStylePitchModDepth()` を PMS テーブル式へ置換
- 既存式と比較できるよう切替を残す

## Phase 7: Pitch EG

- OPM からは取れないため DX21 側機能として追加
- PL は cent 変換
- PR は指数時間テーブルから開始

---

# 14. 具体的なコード追加案

```cpp
enum class LevelModel
{
    CurrentDx21Native,
    ChipLikeOpm127,
    Hybrid
};

constexpr double kOpmTlDbPerStep = 0.75;
constexpr double kChipLevelBlend = 0.50;
constexpr double kChipModulatorBlend = 0.50;
constexpr double kChipModulatorIndexScale = 1.00;

inline double dx21LevelToOpmTl(double level)
{
    level = clampDouble(level, 0.0, 99.0);
    return std::round((1.0 - level / 99.0) * 127.0);
}

inline double opmTlToDb(double tl)
{
    return clampDouble(tl, 0.0, 127.0) * kOpmTlDbPerStep;
}

inline double dbToAmplitude(double db)
{
    return std::pow(10.0, -db / 20.0);
}

inline double opmTlToAmplitude(double tl)
{
    return dbToAmplitude(opmTlToDb(tl));
}

inline double outputLevelToCarrierAmplitudeChipLike(double level)
{
    return opmTlToAmplitude(dx21LevelToOpmTl(level));
}

inline double outputLevelToCarrierAmplitudeHybrid(double level, double chipAmount)
{
    const double oldAmp = outputLevelToCarrierAmplitude(level);
    const double chipAmp = outputLevelToCarrierAmplitudeChipLike(level);

    chipAmount = clampDouble(chipAmount, 0.0, 1.0);

    const double oldDb = -20.0 * std::log10(std::max(oldAmp, 1.0e-9));
    const double chipDb = -20.0 * std::log10(std::max(chipAmp, 1.0e-9));
    const double db = oldDb * (1.0 - chipAmount) + chipDb * chipAmount;

    return dbToAmplitude(db);
}

inline double outputLevelToModulatorIndexChipLike(double level)
{
    return outputLevelToCarrierAmplitudeChipLike(level) * kChipModulatorIndexScale;
}

inline double outputLevelToModulatorIndexHybrid(double level, double chipAmount)
{
    const double oldIndex = outputLevelToModulatorIndex(level);
    const double chipIndex = outputLevelToModulatorIndexChipLike(level);
    chipAmount = clampDouble(chipAmount, 0.0, 1.0);
    return oldIndex * (1.0 - chipAmount) + chipIndex * chipAmount;
}
```

---

# 15. renderOperator 内の置換案

## 現行

```cpp
const double level = nextOperatorLevel(opIndex, op.level);
const double scaledLevel = keyboardScaledLevel(level, op.levelScale, midiNote);
const double velocityBoost = 1.0 + static_cast<double>(op.velocity) * 0.08
    * (static_cast<double>(noteVelocity) / 127.0 - 0.5);
const double ampMod = op.ampModEnable ? operatorAmpModFactor(ampDepth, lfoAm) : 1.0;

const double carrierAmp = outputLevelToCarrierAmplitude(scaledLevel) * envelopeAmp * velocityBoost * ampMod;
const double modulatorIndex = outputLevelToModulatorIndex(scaledLevel) * envelopeAmp * velocityBoost * ampMod
    * modulatorAttackSoftening(ageSeconds);
```

## Phase 1 置換案

```cpp
const double level = nextOperatorLevel(opIndex, op.level);
const double scaledLevel = keyboardScaledLevel(level, op.levelScale, midiNote);
const double velocityBoost = 1.0 + static_cast<double>(op.velocity) * 0.08
    * (static_cast<double>(noteVelocity) / 127.0 - 0.5);
const double ampMod = op.ampModEnable ? operatorAmpModFactor(ampDepth, lfoAm) : 1.0;

const double carrierLevelAmp = outputLevelToCarrierAmplitudeHybrid(scaledLevel, kChipLevelBlend);
const double modIndex = outputLevelToModulatorIndexHybrid(scaledLevel, kChipModulatorBlend);

const double carrierAmp = carrierLevelAmp * envelopeAmp * velocityBoost * ampMod;
const double modulatorIndex = modIndex * envelopeAmp * velocityBoost * ampMod
    * modulatorAttackSoftening(ageSeconds);
```

## Phase 2 置換案

`nextOperatorLevel()` を `nextOperatorTl()` に変更した後の形。

```cpp
const double tl = nextOperatorTl(opIndex, op.level);
const double tlWithKeyScale = clampDouble(tl + keyboardScaleTlOffset(midiNote, op.levelScale), 0.0, 127.0);
const double levelDb = opmTlToDb(tlWithKeyScale);
const double envDb = ampToDb(envelopes[opSize].next());
const double velocityDb = velocityDbOffset(op.velocity, noteVelocity);
const double amDb = op.ampModEnable ? ampModDbOffset(ampDepth, lfoAm) : 0.0;

const double totalDb = levelDb + envDb + velocityDb + amDb;
const double operatorAmp = dbToAmplitude(totalDb);

const double carrierAmp = operatorAmp;
const double modulatorIndex = operatorAmp * kChipModulatorIndexScale
    * modulatorAttackSoftening(ageSeconds);
```

---

# 16. 期待される音色変化

## Carrier LEVEL を Chip-like にすると

- 中低 LEVEL の音量が下がる
- 並列アルゴリズムの濁りが減る可能性がある
- パッド系は細く感じる可能性がある
- ベル系、エレピ系は整理される可能性がある

## Modulator LEVEL を Chip-like にすると

- 変調過多が減る可能性がある
- 金属感が弱くなる可能性がある
- 倍音の出方が自然になる可能性がある
- 物足りない場合は `kChipModulatorIndexScale` を上げる

## EG dB 統合をすると

- 減衰の見通しがよくなる
- Keyboard Scaling / Velocity / AM を同じ減衰モデルへ統合しやすい
- 既存音色との差分確認が必要

## LFO PMS テーブル化をすると

- PMS 0–5 の浅いビブラートが扱いやすくなる
- PMS 6–7 の大きな揺れが OPM 風になる
- DX21実機のPMSと完全一致する保証はない

---

# 17. 注意点

## 17.1 Nuked-OPM は DX21 そのものではない

Nuked-OPM は YM2151 / YM2164 / OPP のチップ挙動を再現する。
DX21 の音色パラメータ体系、特に Pitch EG や DX21 固有の変換テーブルは、Nuked-OPM から直接は得られない。

したがって、チップ寄せで得られるのは主に以下である。

- TL の減衰量モデル
- EG + TL の加算的な考え方
- Operator bus / Phase modulation の考え方
- LFO PMS / PMD の段階的な考え方
- TL Ramp の平滑化構造

## 17.2 現行音色を壊さないために切替を残す

必ず以下を残す。

```text
Current
ChipLike
Hybrid
```

音色比較と後戻りを可能にする。

## 17.3 最初から完全置換しない

最初に変更するのは、Carrier / Modulator の LEVEL カーブのみでよい。
EG、AM、Velocity、Pitch EG は後段階でよい。

---

# 18. 最小変更セット

最初のコミットで行うべき最小変更は以下。

```text
1. dx21LevelToOpmTl() を追加
2. opmTlToDb() を追加
3. opmTlToAmplitude() を追加
4. outputLevelToCarrierAmplitudeChipLike() を追加
5. outputLevelToCarrierAmplitudeHybrid() を追加
6. outputLevelToModulatorIndexChipLike() を追加
7. outputLevelToModulatorIndexHybrid() を追加
8. renderOperator() で Hybrid を使用
9. kChipLevelBlend / kChipModulatorBlend を定数化
```

この時点では、EG、LFO、Feedback、Pitch EG は変更しない。

---

# 19. 最初に試す推奨値

```cpp
constexpr double kChipLevelBlend = 0.50;
constexpr double kChipModulatorBlend = 0.50;
constexpr double kChipModulatorIndexScale = 1.08;
constexpr double kOpmTlDbPerStep = 0.75;
```

音が細い場合:

```cpp
kChipLevelBlend = 0.25;
kChipModulatorIndexScale = 1.16;
```

音が明るすぎる場合:

```cpp
kChipModulatorBlend = 0.75;
kChipModulatorIndexScale = 1.00;
```

音量全体が下がる場合:

```cpp
kCarrierMixGain を上げる
```

ただし、音量補正と音色補正は分けて考える。

---

# 20. 結論

実測なしで可能な限りチップに寄せるなら、最も効果が大きく、かつ根拠がある修正は以下である。

```text
DX21 LEVEL を直接振幅へ変換しない。
まず YM2151 / OPP 的な TL 減衰量へ変換し、
TL -> dB -> amplitude の流れで Carrier / Modulator を生成する。
```

最初は完全置換ではなく Hybrid として導入する。

最終的な理想構造は以下である。

```text
DX21 LEVEL
    ↓
OPM TL target
    ↓
OPP TL Ramp 風 smoothing
    ↓
TL / dB attenuation
    ↓
EG / Keyboard Scaling / Velocity / AM と統合
    ↓
Operator amplitude
    ↓
Carrier audio または Modulator bus
```

この構造に移すことで、現行の DX21Native の音色再現を維持しつつ、Nuked-OPM 由来のチップ的な考え方を段階的に取り込める。
