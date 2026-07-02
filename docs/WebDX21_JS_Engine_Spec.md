# WebDX21 JS DX21エンジン仕様書（C++移植用）

## 1. 目的と適用範囲

本書は WebDX21 に実装されている JavaScript 音源エンジン（`dx21-worklet.js`）の現行仕様を、C++/JUCE へ移植するために整理したものである。

対象:
- 音源コア（4 Operator FM、EG、LFO、ボイス管理、出力整形）
- Worklet メッセージ I/F（patch / noteOn / noteOff / pitchBend / modWheel / panic）
- DX21 VMEM 由来データとの接続点

非対象:
- 画面 UI 実装
- WebAudio のエフェクトチェーン（Reverb/Delay/Chorus）
- OPM WASM エンジン本体

注記:
- 本エンジンは「DX21風」実装であり、実機の完全ビット一致エミュレーションではない。

---

## 2. 全体アーキテクチャ

### 2.1 処理レイヤ

1. UI/Host レイヤ（`main.js`）
- パッチ編集結果を `patch` メッセージで送信
- MIDI イベントを `noteOn/noteOff/pitchBend/modWheel` へ変換

2. AudioWorklet Processor（`DX21Processor`）
- ボイス配列（最大 8、低電力デバイス時 4）を保持
- サンプルごとに全ボイスを加算
- limiter + declick を適用して stereo 同値出力

3. Voice（`Voice`）
- 4 Operator の FM 計算
- アルゴリズム依存の依存関係（modulator -> carrier）を再帰計算
- Envelope/LFO/Detune/Feedback/Keyboard scaling を統合

4. Envelope（`DX21Envelope`）
- Attack/Decay1/Decay2/Release の 4 ステージ
- attenuation(dB) 基準で進行

### 2.2 サンプリング・出力

- サンプルレート: Worklet の `sampleRate`
- 出力: mono 計算値を L/R 同値で出力
- クリッピング保護:
  - `limit(sample) = tanh(sample * 0.92) * 0.98`
- クリック抑制:
  - 1サンプル差分を `[-0.42, +0.42]` で制限

---

## 3. パッチデータ仕様

## 3.1 ルート構造

```ts
Patch {
  algorithm: number; // 1..8
  feedback: number;  // 0..7
  transpose: number; // semitone
  lfo: {
    speed: number;            // 0..99
    delay: number;            // 0..99
    pitchDepth: number;       // 0..99
    ampDepth: number;         // 0..99
    pitchSensitivity: number; // 0..7
    ampSensitivity: number;   // 0..3
    sync: boolean;
    wave: number;             // 0..3
  };
  operators: Operator[4];
}
```

```ts
Operator {
  ratioIndex: number;   // DX21_RATIOS index
  detune: number;       // -3..+3
  level: number;        // 0..99
  rateScale: number;    // 0..3
  levelScale: number;   // 0..99
  velocity: number;     // 0..7
  ampModEnable?: bool;  // 未指定時は true 扱い
  envelope: {
    attackRate: number;  // 0..31
    decay1Rate: number;  // 0..31
    decay1Level: number; // 0..15
    decay2Rate: number;  // 0..31
    releaseRate: number; // 0..15
  }
}
```

### 3.2 正規化

`normalizePatch()` により不足項目を補完する。

既定値（主要項目）:
- algorithm=1, feedback=2, transpose=0
- lfo={speed:24, delay:0, pitchDepth:0, ampDepth:0, pitchSensitivity:3, ampSensitivity:0, sync:false, wave:0}
- operator={ratioIndex:4, detune:0, level:70, rateScale:0, levelScale:0, velocity:2, ampModEnable:true}
- envelope={20,8,12,1,6}

---

## 4. アルゴリズム仕様（4OP）

内部 index は `[0..3]` で、OPM slot 対応は以下。
- index 0 = OP1(C2)
- index 1 = OP2(M2)
- index 2 = OP3(C1)
- index 3 = OP4(M1)

各アルゴリズムは `carriers` と `deps` で定義される。

```ts
Algorithm {
  carriers: number[]; // 最終加算対象
  deps: number[][];   // deps[i] は op[i] へ位相加算する modulator index 群
}
```

実装定義（algorithm 1..8）:
1. `4>3>2>1`
2. `4+3>2>1`
3. `3>2 + 4>1`
4. `4>3 + 2>1`
5. `2>1 + 4>3`
6. `4>(1+2+3)`
7. `4>3 + 1 + 2`
8. `1 + 2 + 3 + 4`

---

## 5. 周波数・位相仕様

### 5.1 Ratio table

`DX21_RATIOS`（64要素）を直接使用。

### 5.2 基本周波数

- `baseFrequency = midiToFrequency(note + transpose) * 2^((bend*2 + appliedPitchLfo)/12)`
- `midiToFrequency(n) = 440 * 2^((n - 69)/12)`

Pitch Bend は ±2 semitone 固定。

### 5.3 Operator 周波数

- `frequency = baseFrequency * ratio + detuneOffset`
- detuneOffset は OPM DT1 近似計算（`opmStyleDt1FrequencyOffset()`）

### 5.4 位相進行

- 位相ステップ精度: `2^20`
- `increment = round(clamp(freq, 0, sampleRate*0.49) * 2^20 / sampleRate)`
- 最終位相加算値: `increment * 2π / 2^20`

---

## 6. Envelope Generator（EG）仕様

### 6.1 基本モデル

- 管理量: attenuation[dB]
- 初期値: `EG_QUIET_DB = 96`
- ステージ: `off -> attack -> decay1 -> decay2 -> release -> off`
- EG tick 基準: `DX21_EG_TICK_HZ = 3579545 / 64 / 3`

`next()` ごとに:
1. `egRemainder += DX21_EG_TICK_HZ / sampleRate`
2. `egRemainder >= 1` の回数だけ tick 進行
3. attenuation から amp へ変換

### 6.2 D1L 変換

- `decay1Level (0..15)` を dB へ変換:
- `if level >= 15 then 0dB else (15 - level) * 3`

### 6.3 レート計算

- Attack/Decay は 0..31、Release は 0..15
- キースケール補正あり
- 実効レートは 0..63 に clamp
- テーブル `EG_INC` と `shift/select` で増分量を決定

主要定数:
- `DX21_ATTACK_RATE_SCALE = 2`
- `DX21_DECAY_RATE_SCALE = 1.85`
- `DX21_RELEASE_RATE_SCALE = 3.75`
- `DX21_RELEASE_RATE_BIAS = 2`

---

## 7. レベル・変調仕様

### 7.1 Carrier レベル

- `level(0..99)` -> 振幅
- dBレンジ: `DX21_CARRIER_LEVEL_DB_RANGE = 48`

`carrierAmp = 10^(-((1 - level/99) * 48)/20)`

### 7.2 Modulator 指数

carrier と別カーブで FM index を生成。

主要定数:
- `DX21_MODULATOR_INDEX_SCALE = 1.08`
- `DX21_MODULATOR_INDEX_BLEND = 0.08`
- `DX21_MODULATOR_INDEX_EXPONENT = 2.2`

### 7.3 Modulator Attack Softening

発音直後の変調量のみ緩和。

- 期間: `DX21_MODULATOR_ATTACK_SOFTEN_SECONDS = 0.012`
- 初期係数: `DX21_MODULATOR_ATTACK_INITIAL_SCALE = 0.86`
- 経過で 1.0 へ線形遷移

### 7.4 TL Ramp 近似

パラメータ変化時の段差を緩和。

- `OPP_TL_RAMP_SECONDS = 0.012`
- `OPP_TL_SUBSTEPS = 8`
- 内部 TL 単位をサブステップで追従

### 7.5 Keyboard Scaling

- `levelScale` は高音域でレベル減衰
- 近似式:
  - `highKeyAmount = clamp((note - 60)/36, 0, 1)`
  - `scaledLevel = level - highKeyAmount * levelScale * 0.45`

---

## 8. LFO 仕様

### 8.1 速度

- `dx21LfoSpeedToHz(speed)`
- `speed(0..99)` を非線形マップ
- `speed=0` は 0.01Hz 相当

### 8.2 波形

`wave`:
- 0: Saw Up
- 1: Square
- 2: Triangle
- 3: S/Hold（ノイズテーブル）

AM は `[0..1]`、PM は `[-1..1]` に clamp。

### 8.3 Vibrato OSC 互換

`pitchSensitivity == 0` かつ `pitchDepth > 0` のとき、Pitch LFO は triangle を使用。

### 8.4 Delay

- `waitSeconds = 0.25 * 2^(delay/25)`
- 高 delay 領域で fade を拡張
- 出力係数 `0..1`

### 8.5 AM 適用

- operator 単位で適用（最終mixではない）
- `ampModEnable === false` の operator は AM 無効
- 未指定は互換目的で有効扱い

---

## 9. Feedback 仕様

- 適用対象: operator index 3（OP4）
- 2サンプル履歴を使用: `feedback[0]`, `feedback[1]`
- `patch.feedback(0..7)` をシフト係数へ変換し位相へ加算

---

## 10. FM 計算手順（1サンプルあたり）

1. グローバルLFO時刻を更新
2. 各 voice について:
- LFO（AM/PM）と delay 係数を計算
- pitchLfo を1サンプル遅延適用（`nextPitchModulation`）
- baseFrequency を算出
- アルゴリズム依存で operator を再帰計算
- carriers を加算し、carrier数で正規化
3. 全 voice 合算
4. `JS_FM_OUTPUT_GAIN = 0.56` を乗算
5. limiter + declick
6. L/R へ同値書き込み

異常値対策:
- 非有限値（NaN/Inf）検出時は当該 voice を fail 扱いで除外

---

## 11. ボイス管理仕様

- 最大同時発音: 1..8（通常 8）
- `noteOn`:
  - 同 note を先に除去
  - 新規 Voice を push
  - 超過時は先頭を drop
- `noteOff`:
  - 該当 note の Voice を release へ
- `panic`:
  - voices 全消去
  - `lastOutput` と `globalLfoAge` をリセット

Voice active 条件:
- `!failed` かつ いずれかの envelope が `off` 以外

---

## 12. Processor メッセージI/F

### 12.1 Input messages

- `patch`: Patch 全体更新
- `noteOn`: `{ note, velocity }`
- `noteOff`: `{ note }`
- `pitchBend`: `-1..+1`
- `modWheel`: `0..1`
- `panic`: payload なし

### 12.2 Output messages（エラー通知）

- `error`: Processor 全体エラー
- `voiceError`: voice 単位エラー

スロットリング:
- 0.5秒以内の連続通知は抑制

---

## 13. MIDI 入力仕様（host側）

`midi.js` における変換:
- Note On (`0x90`, vel>0) -> `noteOn`
- Note On (`0x90`, vel=0) -> `noteOff`
- Note Off (`0x80`) -> `noteOff`
- Pitch Bend (`0xE0`) -> `((msb<<7)+lsb-8192)/8192`
- CC#1 (`0xB0`, controller=1) -> `modWheel = value/127`

---

## 14. DX21 VMEM 連携仕様

- `DX21.syx`（32 voice bulk）を読み込み
- 1voice=128byte
- VMEM operator block 順: `OP4, OP2, OP3, OP1`
- UI順へ再配置: `[3,1,2,0]`

復元される主要項目:
- algorithm, feedback
- LFO speed/delay/pitchDepth/ampDepth/sync/wave/pitchSensitivity/ampSensitivity
- operator EG, level, ratioIndex, detune, rateScale, velocity, ampModEnable

---

## 15. C++移植ガイド（実装分割推奨）

### 15.1 クラス分割

- `Dx21Patch` / `Dx21Operator` / `Dx21EnvelopeParams`
- `Dx21Envelope`
- `Dx21Voice`
- `Dx21Engine`（voice pool、MIDIイベント、renderBlock）
- `Dx21Tables`（ratio/sine/EG_INC/noise 等）

### 15.2 実装順序

1. テーブルと数学関数を固定
2. Envelope 単体の波形検証
3. Voice 単体（アルゴリズム固定）検証
4. 全アルゴリズム対応
5. Engine（polyphony / MIDI）統合
6. 出力 limiter/declick 追加

### 15.3 数値再現上の注意

- JS `Number` は 64bit float。C++は原則 `double` で揃えてから必要箇所だけ `float` 化
- テーブル境界 clamp を厳密に再現
- `wrapPhase` や feedback などの 1サンプル遅延挙動を一致させる
- 非有限値の防御（isfinite）を必ず入れる

---

## 16. 既知の仕様差・割り切り

- 実機DX21の厳密挙動（DAC、タイミング、ノイズ、アナログ段）は未再現
- JS版は OPM 互換要素（DT1近似、TL ramp 風）を取り込みつつ、DX21操作感を優先
- 将来のネイティブ版では「互換モード（JS準拠）」と「高品位モード（改良版）」の分離が望ましい

---

## 17. 参照ソース

- `WebDX21/src/dx21-worklet.js`
- `WebDX21/src/algorithm.js`
- `WebDX21/src/constants.js`
- `WebDX21/src/dx21-sysex.js`
- `WebDX21/src/midi.js`
- `WebDX21/docs/DX21_WebSynth_Spec.md`
