# DX21 LEVEL → YM2151 / YM2164 OPP TL 変換・加算仕様

## 目的

DX21の各Operatorの `LEVEL` パラメータを、Nuked-OPM / YM2151系の `TL（Total Level）` 加算系へ接続するための仕様をまとめる。

ここで扱うのは **Operatorの音量・変調量の制御** であり、Pitch EGではない。

- DX21側：`LEVEL = 0–99`
- YM2151側：`TL = 0–127`
- Nuked-OPM内部：`TL << 3` した10bit相当の減衰量としてEG出力へ加算
- YM2164 / OPP側：`TL Ramp` により `TL` を滑らかに変化させることが可能

---

## 基本概念

### DX21 LEVEL

DX21のOperator `LEVEL` は、Operatorの出力レベルを表す。

```text
LEVEL 99 = 最大出力
LEVEL 0  = 最小出力
```

ただし、YM2151の `TL` は逆向きの量である。

### YM2151 TL

YM2151の `TL` は **Total Level = 減衰量** である。

```text
TL 0   = 減衰なし = 最大音量
TL 127 = 最大減衰 = 最小音量
```

したがって、DX21 `LEVEL` からYM2151 `TL` へ変換するときは、大小関係が反転する。

```text
DX21 LEVEL が大きい → YM TL は小さい
DX21 LEVEL が小さい → YM TL は大きい
```

---

## Nuked-OPMにおけるYM2151 TLの処理

### TLレジスタ保持

通常YM2151モードでは、TLレジスタは7bitとして保持される。

```c
sl_tl[slot] = reg_data & 0x7f;
```

つまり有効範囲は以下。

```text
TL = 0–127
```

---

## TLの加算位置

Nuked-OPMでは、Operator EGの出力に `TL << 3` が加算される。

概念的には以下の形になる。

```c
eg_out = eg_level + am;
eg_out = clamp(eg_out, 0, 1023);

eg_out = eg_out + (tl << 3);
eg_out = clamp(eg_out, 0, 1023);
```

つまり、YM2151のTL 1 stepは、内部EGドメインでは8 stepに相当する。

```text
1 TL step = 8 EG internal units
```

---

## Operator出力での最終減衰

Operator演算では、波形テーブル由来の `logsin` に、EG出力をさらに左シフトした値が加算される。

```c
op_atten = logsin + (eg_out << 2);
op_atten = clamp(op_atten, 0, 4095);
```

したがって、TL 1 stepは最終Operator attenuation上では以下になる。

```text
1 TL step
= 8 EG internal units
= 8 << 2
= 32 operator attenuation units
```

まとめると以下。

```text
TL 1 step = EG加算 8 = Operator減衰加算 32
```

---

## 通常YM2151モードでの実装式

DX21の `LEVEL` からYM2151の `TL` へ変換する関数を以下のように定義する。

```c
uint8_t DX21_LevelToTL7(uint8_t level99);
```

この関数の戻り値は以下の範囲とする。

```text
return value = 0–127
0   = 最大出力
127 = 最小出力
```

通常YM2151モードでは、Operatorごとに以下を設定する。

```c
tl7 = DX21_LevelToTL7(dx21_operator_level);
sl_tl[slot] = tl7;
```

Nuked-OPM内部で実際に加算される値は以下。

```c
tl_add10 = tl7 << 3;
```

最終的なEG出力は以下。

```c
effective_eg10 = clamp10(operator_eg10 + am10 + (tl7 << 3));
```

Operator attenuationは以下。

```c
op_atten12 = clamp12(logsin12 + (effective_eg10 << 2));
```

---

## DX21 LEVEL変換層

DX21の `LEVEL 0–99` は、そのまま線形音量として扱わない。

必ず一度、YM系の **log attenuation / TLドメイン** へ変換する。

### 推奨インターフェース

```c
static const uint8_t DX21_LEVEL_TO_TL[100] = {
    /* 0..99 の既知変換テーブル */
};

uint8_t DX21_LevelToTL7(uint8_t level)
{
    if (level > 99)
        level = 99;
    return DX21_LEVEL_TO_TL[level];
}
```

### 変換の向き

最低限、以下の関係を満たすこと。

```text
DX21_LevelToTL7(99) ≒ 0
DX21_LevelToTL7(0)  ≒ 127

levelA > levelB なら
DX21_LevelToTL7(levelA) < DX21_LevelToTL7(levelB)
```

### 暫定線形近似

実機再現では既知テーブルまたは実測値を使うべきだが、デバッグ用の暫定近似としては以下が使える。

```c
uint8_t DX21_LevelToTL7_Linear(uint8_t level)
{
    if (level > 99)
        level = 99;
    return (uint8_t)((99 - level) * 127 + 49) / 99;
}
```

ただしこれは確認用の近似であり、実機互換を目指す最終仕様にはしない。

---

## CarrierとModulatorでの意味の違い

TL / LEVEL変換式自体はCarrierでもModulatorでも同じでよい。

ただし、聴感上の意味は異なる。

| Operator種別 | LEVEL / TLの効果 |
|---|---|
| Carrier | 最終音量に直接影響する |
| Modulator | 変調指数に影響し、音色の明るさ・倍音量を変える |

したがって、同じ `LEVEL` 値でも、Carrierでは音量変化として聞こえ、Modulatorでは音色変化として聞こえる。

---

# YM2164 / OPP TL Ramp仕様

## 位置づけ

YM2164 / OPPモードでは、TLを即時変更するだけでなく、目標TLへ滑らかに近づける `TL Ramp` がある。

これはDX21のPitch EGとは別物であり、**Operator Total Levelの時間変化** である。

---

## OPPモードでのTL保持

OPPモードでは、TLレジスタのbit7をRamp有効フラグとして使う。

```text
bit7 = 0: 通常TL。即時反映
bit7 = 1: TL Ramp有効。bit0–6を目標TLとして扱う
```

```c
uint8_t tl_reg = sl_tl[slot];

if ((tl_reg & 0x80) == 0) {
    // 通常TL
    opp_tl[slot] = tl_reg << 3;
} else {
    // TL Ramp
    target_tl = tl_reg & 0x7f;
}
```

---

## TL Rampの内部単位

OPPのRamp済みTLは、Nuked-OPM内部では `opp_tl[slot]` に保持される。

```text
opp_tl = TLを8倍した内部値
```

つまり、通常TLと同じく以下の単位で扱う。

```text
1 TL = 8 internal units
```

Ramp時は、この内部値を1 stepずつ増減する。

```text
1 Ramp更新 = ±1 internal unit = ±1/8 TL
```

---

## TL Ramp更新式

Ramp有効時、カウンタが一致したタイミングで、現在のTLを目標TLへ1/8 TLずつ近づける。

```c
uint8_t target_tl = sl_tl[slot] & 0x7f;
uint8_t current_tl_int = opp_tl[slot];
uint8_t current_tl7 = current_tl_int >> 3;

if (match) {
    if (current_tl7 < target_tl)
        opp_tl[slot]++;
    else if (current_tl7 > target_tl)
        opp_tl[slot]--;
}
```

厳密には、比較は `opp_tl[slot] >> 3` で行い、増減は `opp_tl[slot]` に対して ±1 で行う。

このため、RampはTL単位ではなく、TLの1/8単位で滑らかに動く。

---

## Ramp速度

チャンネルごとの `ramp_div` と `opp_tl_cnt` で更新間隔を決める。

```c
ramp = ch_ramp_div[channel];
match = ramp == opp_tl_cnt[channel];
```

概念的には以下。

```text
ramp_div が小さい → 速い
ramp_div が大きい → 遅い
```

1回のRamp更新で `opp_tl` は1 internal unit、つまり1/8 TLだけ動く。

概算では以下。

```text
1 internal step ≒ 32 * (ramp_div + 1) OPM_Clock calls
1 TL step       ≒ 256 * (ramp_div + 1) OPM_Clock calls
```

---

## OPP TL RampをDX21 LEVELに使う場合

DX21 Operator `LEVEL` をOPP TL Rampで滑らかに変えたい場合、以下のように接続する。

```c
uint8_t target_tl7 = DX21_LevelToTL7(dx21_operator_level);

if (ramp_enabled) {
    sl_tl[slot] = 0x80 | target_tl7;
} else {
    sl_tl[slot] = target_tl7;
}
```

`ramp_enabled` が有効な場合、Nuked-OPM側では `opp_tl[slot]` が現在値から `target_tl7` へ滑らかに近づく。

---

## 通常YM2151モードとOPPモードの比較

| 項目 | 通常YM2151 | YM2164 / OPP |
|---|---|---|
| TL範囲 | 0–127 | 0–127 + bit7 Ramp flag |
| 反映 | 即時 | 即時またはRamp |
| 内部加算 | `TL << 3` | `opp_tl` |
| Ramp | なし | あり |
| 1 TL | 8 internal units | 8 internal units |

---

# 実装フロー

## Voice読み込み時

```c
for each operator:
    level99 = dx21_voice.operator[i].level;
    tl7 = DX21_LevelToTL7(level99);
    sl_tl[slot] = tl7;
```

## LEVEL変更時

通常YM2151互換で即時変更する場合：

```c
sl_tl[slot] = DX21_LevelToTL7(new_level99);
```

OPP TL Rampを使って滑らかに変更する場合：

```c
sl_tl[slot] = 0x80 | DX21_LevelToTL7(new_level99);
```

## 発音中の計算

```text
1. Operator EGを計算
2. AMが有効ならLFO AMを加算
3. TLを加算
   - 通常YM2151: TL << 3
   - OPP Ramp: opp_tl
4. 10bitでclamp
5. logsinに eg_out << 2 を加算
6. 12bit attenuationとしてOperator出力を求める
```

---

# 疑似コードまとめ

```c
uint8_t DX21_LevelToTL7(uint8_t level99)
{
    if (level99 > 99)
        level99 = 99;
    return DX21_LEVEL_TO_TL[level99];
}

uint16_t DX21_LevelToTLAdd10(uint8_t level99)
{
    return (uint16_t)DX21_LevelToTL7(level99) << 3;
}

uint16_t CalcEffectiveEG_NormalYM2151(
    uint16_t eg10,
    uint16_t am10,
    uint8_t dx21_level99
) {
    uint16_t tl_add10 = DX21_LevelToTLAdd10(dx21_level99);
    return clamp10(eg10 + am10 + tl_add10);
}

uint16_t CalcEffectiveEG_OPP(
    uint16_t eg10,
    uint16_t am10,
    uint16_t opp_tl10
) {
    return clamp10(eg10 + am10 + opp_tl10);
}
```

---

# 検証項目

最低限、以下を確認する。

```text
LEVEL 99 → 最大出力に近い
LEVEL 0  → 最小出力に近い
LEVELを上げるとTLが下がる
LEVELを下げるとTLが上がる
CarrierのLEVELを変えると音量が変わる
ModulatorのLEVELを変えると倍音・音色が変わる
OPP Ramp有効時、LEVEL変更が段差ではなく滑らかになる
```

---

# 注意点

## 線形振幅に変換しない

DX21 LEVELを直接PCM振幅に掛ける実装は避ける。

FM音源内部では、Operator出力はlog attenuation系で処理されるため、以下の流れを守る。

```text
DX21 LEVEL
→ YM TL / attenuation
→ EGへ加算
→ logsin attenuationへ加算
→ exp変換
→ Operator出力
```

## TLは音量ではなく減衰量

`TL` は音量ではなく減衰量である。

```text
TLを上げる = 音を小さくする
TLを下げる = 音を大きくする
```

## CarrierとModulatorの違いを混同しない

同じLEVEL変換を使っても、CarrierとModulatorでは結果が異なる。

```text
Carrier LEVEL   → 音量
Modulator LEVEL → 変調量・倍音量
```

---

# 結論

DX21のOperator `LEVEL` は、YM2151 / YM2164系の `TL` と同じく、Operator出力を制御するパラメータとして扱える。

ただし、方向は逆である。

```text
DX21 LEVEL 99 → YM TL 0付近
DX21 LEVEL 0  → YM TL 127付近
```

Nuked-OPM内部では、通常YM2151では `TL << 3` をEGに加算し、YM2164 / OPPでは `opp_tl` を加算する。

したがって、実装上の中心は以下である。

```text
DX21 LEVEL 0–99
→ DX21_LevelToTL7()
→ TL 0–127
→ TL << 3 または OPP opp_tl
→ EG出力へ加算
```
