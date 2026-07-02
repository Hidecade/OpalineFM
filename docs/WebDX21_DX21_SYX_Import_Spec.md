# WebDX21 DX21.syx Voice取り込み仕様書

## 1. 目的

本仕様書は、WebDX21 が `voice/DX21.syx` から 32 Voice データを読み込み、UI状態および音源パラメータへ反映する処理をコード準拠で定義する。

対象コード:
- `src/main.js`
- `src/dx21-sysex.js`
- `src/ui.js`
- （補助）`scripts/update-presets-from-syx.js`

本書は実装仕様であり、DX21 SysEx規格の一般解説ではない。

---

## 2. 取り込みフロー

### 2.1 起動時ロード

起動時に `main.js` から以下の順で実行される。

1. `fetch("voice/DX21.syx?v=20260629-vmem")`
2. `response.arrayBuffer()`
3. `parseDx21BulkVmem(buffer)`
4. 戻り値を `vmemPresets` として `setupUI({ vmemPresets, ... })` へ渡す

### 2.2 エラー時挙動

`fetch` 失敗、形式不正、デコード失敗時は例外を捕捉して警告ログを出し、空配列 `[]` を返す。

結果:
- アプリは継続動作する
- `constants.js` の `PRESETS` のみで動作
- 起動停止はしない

---

## 3. DX21 Bulk SysEx 検証と分解

`parseDx21BulkVmem(bytes)` は次を必須条件とする。

- 総サイズ `>= 4104`
- 先頭バイト `0xF0`
- 末尾バイト `0xF7`

不一致時は `Expected a DX21 32-voice bulk SysEx file.` を送出する。

### 3.1 32 Voice領域

- Voiceデータ本体は `data.subarray(6, 6 + 4096)`
- 1 Voice あたり 128 byte
- Voice数は固定 32

各 Voice について返却される中間形式:

```ts
{
  name: string,      // vmem[57..66] ASCII trim
  vmem: number[]     // 128byte配列
}
```

---

## 4. VMEM 1Voice デコード仕様

`decodeDx21VmemVoice(vmemBytes)` は 128byte 未満をエラーとする。

### 4.1 グローバルパラメータ

- `packed = vmem[40]`
- `algorithm = (packed & 0x07) + 1`
- `feedback = (packed >> 3) & 0x07`
- `lfo.sync = Boolean((packed >> 6) & 0x01)`

LFO:
- `speed = vmem[41]`
- `delay = vmem[42]`
- `pitchDepth = vmem[43]`
- `ampDepth = vmem[44]`
- `wave = vmem[45] & 0x03`
- `pitchSensitivity = (vmem[45] >> 2) & 0x07`
- `ampSensitivity = (vmem[45] >> 5) & 0x03`

Voice name:
- `vmem[57..66]` ASCII を trim

### 4.2 Operatorブロック並び替え

VMEMの物理順:
- OP4, OP2, OP3, OP1

実装マップ:
- `DX21_VMEM_OPERATOR_ORDER = [3, 1, 2, 0]`

処理:
- `block = 0..3`, `base = block * 10`
- `opIndex = DX21_VMEM_OPERATOR_ORDER[block]`
- `operators[opIndex]` へ展開

### 4.3 Operator項目

- `attackRate = vmem[base + 0]`
- `decay1Rate = vmem[base + 1]`
- `decay2Rate = vmem[base + 2]`
- `releaseRate = vmem[base + 3]`
- `decay1Level = vmem[base + 4]`
- `levelScale = vmem[base + 5]`
- `ampModEnable = Boolean(vmem[base + 6] & 0x40)`
- `velocity = vmem[base + 6] & 0x07`
- `level = vmem[base + 7]`
- `ratioIndex = vmem[base + 8]`
- `detune = (vmem[base + 9] & 0x07) - 3`
- `rateScale = (vmem[base + 9] >> 3) & 0x03`

---

## 5. UI状態への適用

### 5.1 初期状態

`createInitialState(vmemPresets)`:
- 基本は `structuredClone(PRESETS[0])`
- `vmemPresets[0]` があれば `withVmemPreset` で上書き

### 5.2 プリセット選択時

`presetSelect.change`:
- `PRESETS[presetIndex]` を clone
- `withVmemPreset(clone, vmemPresets[presetIndex])` を適用
- UI と音源へ反映

### 5.3 withVmemPreset の上書きルール

`vmemPreset.vmem` が存在する場合のみ有効。

上書き対象:
- `name`
- `algorithm`
- `feedback`
- `lfo`（マージ）
- `operators`（VMEM由来に置換）
- `vmem`（元128byteを状態保持）

Operator変換は `vmemOperatorToUiOperator` で行い、UI形式に再配置する。

---

## 6. 編集後の挙動（VMEMひも付け解除）

ユーザーがパラメータを編集した時点で `clearVmemPreset(state)` が呼ばれ、`state.vmem` が削除される。

目的:
- 元の生VMEMプリセット由来であることを解除
- 編集後の値を通常パッチとして扱う

解除トリガー例:
- アルゴリズム、FB、Transpose、LFO各種変更
- Operatorノブ変更
- EG変更
- Operator ON/OFF トグル

---

## 7. 音源エンジン側への影響

JS DX21エンジン（`dx21-worklet.js`）は `patch` を受ける際に `normalizePatch()` を通す。

ポイント:
- VMEM由来の `ampModEnable` が保持される
- 欠損項目はデフォルト補完
- その後の発音処理は通常パッチと同一経路

---

## 8. 補助スクリプト仕様（開発用途）

`scripts/update-presets-from-syx.js` は開発補助として、`DX21.syx` から `constants.js` の PRESETS 項目を同期更新する。

主な仕様:
- 入力 `DX21.syx` を同様に 32x128 で分解
- PRESETS 側 name と順序を照合（不一致はエラー）
- algorithm / feedback / lfo / rateScale / levelScale / velocity を置換

注意:
- 実行時取り込み処理そのものではなく、プリセット定義更新のためのオフライン補助である。

---

## 9. 実装上の制約と注意点

- Bulkデータのチェックサム検証は行っていない
- SysEx種別の詳細識別はヘッダ厳密判定ではなく、サイズと先頭末尾バイト中心
- 32 Voice固定前提（不足/過剰データの詳細検証は最小限）
- name は ASCII trim のみで、文字コード変換の追加処理はない

---

## 10. C++移植時の再現要件

移植先（Native/JUCE）では以下の再現を必須とする。

1. 入力検証条件
- `size >= 4104`, `first=0xF0`, `last=0xF7`

2. Voice領域抽出
- `offset=6`, `length=4096`, `voiceSize=128`, `count=32`

3. Operator順変換
- `DX21_VMEM_OPERATOR_ORDER = [3,1,2,0]`

4. packedビット展開
- algorithm, feedback, lfo sync, lfo sensitivity

5. UI/内部パッチへのマージ方針
- VMEMが存在する時のみプリセット上書き
- 編集時にVMEM由来フラグ（生データ保持）を解除

この5項目を一致させることで、Web版と同等の Voice取り込み挙動を確保できる。

---

## 11. 参照

- `WebDX21/src/main.js`
- `WebDX21/src/dx21-sysex.js`
- `WebDX21/src/ui.js`
- `WebDX21/scripts/update-presets-from-syx.js`
