# DX21 Pitch Envelope Generator（PEG）まとめ

## 概要

DX21の **Pitch Envelope Generator（PEG）** は、鍵盤を押してから離すまでの間、音程（ピッチ）を時間的に変化させる機能である。  
ピッチレベルは **50を基準** とし、そこから **±4オクターブ** の範囲で変化させることができる。

主な用途は以下の通り。

- オートベンドのような音程変化
- ドラム音色の「トゥーン」としたピッチ変化
- アタック時だけ音程を上げる／下げる効果
- キーオフ後に音程を変化させる効果

---

## パラメータの基本ルール

全パラメータの設定範囲は **0〜99**。

| 種類 | 意味 | 値の解釈 |
|---|---|---|
| PR（Pitch Rate） | 音程変化の速さ | 0が最も遅く、99が最も速い |
| PL（Pitch Level） | 到達する音程 | 50が基準音程、0が最低音程、99が最高音程 |

PEGの効果をかけたくない場合は、以下のように設定する。

```text
PL1 = 50
PL2 = 50
PL3 = 50
```

Rate値は任意だが、Levelがすべて50であれば音程変化は起きない。

---

## 発音音域に関する注意

DX21の発音音域は **C-1〜B6**。  
PEGによってこの範囲を超える音程が指定された場合、実際には以下の範囲内の音として発音される。

- 低音側で範囲を超えた場合：C-1〜B-1付近
- 高音側で範囲を超えた場合：C6〜B6付近

そのため、ピッチ変化が音域の上限・下限にぶつかると、音程変化が連続的にならない場合がある。

---

## PEGの動作イメージ

PEGは以下の流れで音程を変化させる。

```text
KEY ON
  ↓
PL3 から開始
  ↓ PR1 の速さで変化
PL1 に到達
  ↓ PR2 の速さで変化
PL2 に到達
  ↓
キーを押している間は PL2 を維持
  ↓
KEY OFF
  ↓ PR3 の速さで変化
PL3 に到達
```

`PL2` は持続音の音程になるため、弾いた鍵盤の正しい音程を維持したい場合は **PL2 = 50** に設定する。

---

## 各パラメータ

| 番号 | パラメータ | 名称 | 内容 |
|---:|---|---|---|
| 1 | PR1 | 1st Pitch Rate | 鍵盤を押した瞬間からPL1に達するまでの速さを設定する。 |
| 2 | PL1 | 1st Pitch Level | はじめの音程のピーク、またはディップを設定する。 |
| 3 | PR2 | 2nd Pitch Rate | PL1に達してからPL2に達するまでの速さを設定する。 |
| 4 | PL2 | 2nd Pitch Level | 2番目の音程のピークまたはディップを設定する。この値が持続音の音程になる。 |
| 5 | PR3 | 3rd Pitch Rate | 鍵盤を離した瞬間からPL3に達するまでの速さを設定する。 |
| 6 | PL3 | 3rd Pitch Level | 鍵盤を押した瞬間、または鍵盤を離した後に到達する音程を設定する。 |

---

## 表示例

DX21の画面では、以下のようにパラメータ名と設定値が表示される。

```text
ALG 3 1111
PEG RATE 1 = 50
```

- `PEG RATE 1`：パラメータ名
- `50`：設定値（0〜99）

---

# 実装向け詳細仕様

## 1. Level値の扱い

Pitch Level は **50を中心** に上下へ変化する。

| PL値 | 意味 |
|---:|---|
| 0 | 最低ピッチ |
| 50 | 鍵盤本来のピッチ |
| 99 | 最高ピッチ |

マニュアル上は **±4オクターブ** と説明されているため、実装上は以下のように扱うとよい。

```text
PL = 50 → 0 cent
PL = 99 → +4800 cent
PL = 0  → -4800 cent
```

ただし、50を中心に上下のステップ数が完全対称ではない。

- 50 → 99 は 49段階
- 50 → 0 は 50段階

そのため、単純な線形変換をするなら以下の近似が使える。

```text
if PL >= 50:
    cents = (PL - 50) * 4800 / 49
else:
    cents = (PL - 50) * 4800 / 50
```

周波数倍率に変換する場合は以下。

```text
frequency_multiplier = 2 ^ (cents / 1200)
```

例：

| PL | cents概算 | 音程変化 |
|---:|---:|---|
| 0 | -4800 | -4 octave |
| 25 | -2400 | -2 octave |
| 50 | 0 | 原音程 |
| 75 | 約 +2449 | 約 +2 octave |
| 99 | +4800 | +4 octave |

---

## 2. Rate値の扱い

Pitch Rate は **時間そのものではなく、音程変化の速さ** を表す。

| PR値 | 意味 |
|---:|---|
| 0 | 最も遅い |
| 99 | 最も速い |

重要なのは、同じPR値でも、移動するPitch Levelの距離が大きければ到達までの時間は長くなり、小さければ短くなる点である。

```text
到達時間 ≒ rate_to_time(PR) × 音程移動量
```

より実装寄りに書くと、以下の考え方になる。

```text
segment_time = base_time(PR) * abs(target_cents - start_cents) / 4800
```

ここで `4800` は ±4オクターブ、つまり最大片側移動量をcent換算した値。

---

## 3. 公式資料で未確定の点

DX21の公開マニュアルでは、PR値 0〜99 が実時間で何秒に対応するかの詳細な換算表は確認できない。

確認できるのは以下の範囲である。

- PRは0〜99
- 0が最も遅い
- 99が最も速い
- PLは0〜99
- PL50が原音程
- PL0〜99は50を中心に±4オクターブ相当
- PEGは `PL3 → PL1 → PL2 → PL3` の形で推移する

したがって、エミュレーションでは **PR→実時間の変換テーブルを推定または実測する必要がある**。

---

## 4. 時間推移モデル

### Key On時

鍵盤を押した瞬間、ピッチは `PL3` から始まる。

```text
initial_pitch = PL3
```

### Stage 1

`PR1` の速さで `PL3` から `PL1` へ移動する。

```text
PL3 → PL1
rate = PR1
```

### Stage 2

`PL1` に到達後、`PR2` の速さで `PL2` へ移動する。

```text
PL1 → PL2
rate = PR2
```

### Sustain

キーを押している間は `PL2` を維持する。

```text
while key_is_down:
    pitch = PL2
```

### Key Off時

鍵盤を離すと、`PR3` の速さで `PL3` へ移動する。

```text
current_pitch → PL3
rate = PR3
```

通常の図では `PL2 → PL3` と考えればよいが、実装では、キーオフ時点でまだStage 1またはStage 2の途中である可能性がある。  
その場合は、**キーオフ時点の現在ピッチからPL3へ移動**させる方が自然である。

```text
on_key_off:
    release_start = current_pitch
    target = PL3
    rate = PR3
```

---

## 5. 実装用疑似コード

```pseudo
on_key_on(note):
    peg_stage = 1
    peg_value = PL3
    peg_target = PL1
    peg_rate = PR1

update(dt):
    if peg_stage == 1:
        peg_value = move_toward(peg_value, PL1, PR1, dt)
        if peg_value == PL1:
            peg_stage = 2

    if peg_stage == 2:
        peg_value = move_toward(peg_value, PL2, PR2, dt)
        if peg_value == PL2:
            peg_stage = sustain

    if peg_stage == sustain:
        peg_value = PL2

    if peg_stage == release:
        peg_value = move_toward(peg_value, PL3, PR3, dt)
        if peg_value == PL3:
            peg_stage = finished

on_key_off():
    peg_stage = release
    release_start_value = peg_value
    peg_target = PL3
    peg_rate = PR3
```

---

## 6. 実用的なPR→時間近似

公式換算表がないため、まずは指数的な近似テーブルで実装するのが現実的である。

Rate値はシンセのエンベロープでは線形ではなく、低い値では大きく遅く、高い値では急に速くなる形が多い。  
そのため、以下のような近似が扱いやすい。

```text
base_time(PR) = min_time * (max_time / min_time) ^ ((99 - PR) / 99)
```

例：

```text
min_time = 0.005 sec
max_time = 20.0 sec
```

この場合：

| PR | base_time概算 |
|---:|---:|
| 99 | 0.005 sec |
| 80 | 約0.025 sec |
| 60 | 約0.13 sec |
| 40 | 約0.68 sec |
| 20 | 約3.6 sec |
| 0 | 20.0 sec |

これは公式値ではなく、エミュレーション開始用の仮実装である。  
実機再現を目指す場合は、DX21実機で録音し、PR値ごとの到達時間を測定して補正する。

---

## 7. 実機測定の方法

PR→時間の換算を実測する場合は、以下の条件で測るとよい。

### 測定用音色

- Operator構成は単純なサイン波に近い音色にする
- LFOはOFF
- Pitch Bendは0
- PEG以外のピッチ変化要素を使わない
- PL2 = 50 に固定
- PL3 = 50 に固定
- PR2は測定しない場合、高速または固定値にする

### PR1測定例

```text
PL3 = 50
PL1 = 99
PL2 = 99
PR1 = 測定対象
```

Key Onからピッチが+4 octave付近に到達するまでの時間を測る。

### PR2測定例

```text
PL3 = 50
PL1 = 99
PL2 = 50
PR1 = 99
PR2 = 測定対象
```

Key On後、PL1に到達してからPL2へ戻る時間を測る。

### PR3測定例

```text
PL3 = 50
PL1 = 50
PL2 = 99
PR1 = 99
PR2 = 99
PR3 = 測定対象
```

Key Off後、現在ピッチからPL3へ戻る時間を測る。

---

## 8. SysEx / データ位置

### VMEM形式

| Byte | Parameter |
|---:|---|
| 67 | PITCH EG RATE 1 |
| 68 | PITCH EG RATE 2 |
| 69 | PITCH EG RATE 3 |
| 70 | PITCH EG LEVEL 1 |
| 71 | PITCH EG LEVEL 2 |
| 72 | PITCH EG LEVEL 3 |

### VCED形式

| Parameter No. | Parameter | LCD表示 |
|---:|---|---|
| 87 | PITCH EG RATE 1 | PEG RATE 1 |
| 88 | PITCH EG RATE 2 | PEG RATE 2 |
| 89 | PITCH EG RATE 3 | PEG RATE 3 |
| 90 | PITCH EG LEVEL 1 | LEVEL 1 |
| 91 | PITCH EG LEVEL 2 | LEVEL 2 |
| 92 | PITCH EG LEVEL 3 | LEVEL 3 |

---

## 実用上の設定例

### 1. PEGを無効に近い状態にする

```text
PL1 = 50
PL2 = 50
PL3 = 50
```

Rate値は任意だが、Levelがすべて50であれば音程変化は起きない。

### 2. アタックで一瞬ピッチを上げる

```text
PL3 = 50
PR1 = 高め
PL1 = 60〜70
PR2 = 中程度
PL2 = 50
```

鍵盤を押した直後に音程が上がり、その後正しい音程へ戻る。

### 3. ドラム風にピッチを下げる

```text
PL3 = 80〜99
PR1 = 高め
PL1 = 50前後
PR2 = 中〜高め
PL2 = 30〜50
```

高い音程から始まり、急速に下がることで打楽器的なアタック感を作れる。

---

## 実装・エミュレーション時の要点

DX21互換音源やエディタでPEGを実装する場合は、以下を考慮する。

1. `PL = 50` を基準ピッチとする。
2. `PL` は0〜99を±4オクターブ程度のピッチ変化へ変換する。
3. `PR` は0〜99を時間変化の速さへ変換する。
4. Key On直後の初期値として `PL3` を使う。
5. Key On中は `PL3 → PL1 → PL2` の順に遷移する。
6. Key Off後は `現在値 → PL3` へ `PR3` の速さで遷移する。
7. 発音音域 C-1〜B6 を超える場合は、音程変化が不連続になる可能性がある。
8. PR→秒の公式換算表は確認できないため、実機再現には録音測定が必要である。

---

## 参考メモ

- PEGは操作説明だけなら `PR1/PL1/PR2/PL2/PR3/PL3` の6項目で足りる。
- エミュレーション仕様として使う場合は、`PL→cent変換`、`PR→時間近似`、`Key Off時の現在値からのrelease`、`発音音域によるクリップ/折り返し` を明記しておく方がよい。
