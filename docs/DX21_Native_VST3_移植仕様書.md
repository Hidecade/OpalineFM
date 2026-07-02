# DX21 Native / VST3 移植仕様書

## 1. 目的

WebDX21 の JS DX21 エンジンを C++ に移植し、以下の成果物を同一コードベースで提供する。

1. Windows ネイティブアプリ（Standalone）
2. macOS ネイティブアプリ（Standalone）
3. VST3 プラグイン
4. 将来拡張として AU / CLAP を追加可能な構造

本仕様は、既存の以下3仕様を統合した完成版である。

1. WebDX21_JS_Engine_Spec.md
2. WebDX21_DX21_SYX_Import_Spec.md
3. WebDX21_UI_Spec.md

---

## 2. スコープ

### 2.1 対象

1. 4 Operator FM エンジン本体（JS準拠）
2. DX21.syx の 32 Voice 読み込み
3. Web UI 同等の Standalone 画面
4. VST3 と Standalone の共通 DSP コア
5. プリセット管理、状態保存、MIDI 入力処理

### 2.2 非対象（初期フェーズ）

1. DX21 実機ビット一致エミュレーション
2. Web の CSS 視覚効果完全一致
3. SysEx 送信機能
4. CLAP/AU 実装本体（設計余地のみ確保）

---

## 3. 開発環境

## 3.1 初期開発ターゲット（Windows）

1. VSCode
2. CMake 3.22+
3. MSVC Build Tools v143
4. Windows 10/11 SDK
5. JUCE 8 系
6. Ninja または Visual Studio Generator

## 3.2 将来ターゲット（macOS）

1. Xcode Command Line Tools
2. Apple Clang
3. CMake + JUCE

---

## 4. 成果物

1. dx21_engine 静的ライブラリ
2. Standalone アプリ（JUCE Standalone）
3. VST3 プラグイン
4. テスト用コンソールレンダラ（WAV 出力）

---

## 5. 推奨ディレクトリ構成

```text
DX21Native/
├─ CMakeLists.txt
├─ CMakePresets.json
├─ docs/
│  ├─ DX21_Native_VST3_移植仕様書.md
│  ├─ WebDX21_JS_Engine_Spec.md
│  ├─ WebDX21_DX21_SYX_Import_Spec.md
│  └─ WebDX21_UI_Spec.md
├─ external/
│  └─ JUCE/
├─ assets/
│  └─ DX21.syx
├─ Source/
│  ├─ App/
│  │  ├─ Main.cpp
│  │  └─ MainWindow.*
│  ├─ Plugin/
│  │  ├─ PluginProcessor.*
│  │  └─ PluginEditor.*
│  ├─ Engine/
│  │  ├─ Dx21Tables.*
│  │  ├─ Dx21Types.*
│  │  ├─ Dx21Envelope.*
│  │  ├─ Dx21Voice.*
│  │  ├─ Dx21Engine.*
│  │  ├─ Dx21PatchState.*
│  │  └─ Dx21Sysex.*
│  ├─ UI/
│  │  ├─ PatchPanel.*
│  │  ├─ KeyboardPanel.*
│  │  ├─ OperatorPanel.*
│  │  ├─ ScopeComponent.*
│  │  └─ LcdComponent.*
│  └─ Tests/
│     ├─ EngineTests.*
│     └─ SysexTests.*
└─ presets/
   ├─ factory/
   └─ user/
```

---

## 6. アーキテクチャ

## 6.1 レイヤ分割

1. Engine レイヤ
- FM 演算、EG、LFO、Voice 管理、Patch 正規化
- UI/JUCE 非依存

2. Host Integration レイヤ
- Standalone: AudioDevice + MIDI + UI 更新
- VST3: processBlock + parameter/state I/O

3. UI レイヤ
- Web 構成準拠の画面を JUCE Component で構成
- エンジンへの変更通知は PatchState 経由で一元化

## 6.2 スレッド方針

1. Audio Thread
- DSP 実行のみ
- メモリ確保禁止
- ファイル I/O 禁止

2. Message/UI Thread
- UI 操作
- プリセット読込
- パラメータ編集

3. 同期
- パラメータは lock-free なスナップショット転送を基本とする
- AudioThread から UI を直接触らない

---

## 7. JS DX21 エンジン移植仕様

エンジン挙動は WebDX21_JS_Engine_Spec.md を正とし、以下を必須一致とする。

1. 4OP アルゴリズム 8種
2. DX21_RATIOS（64要素）
3. EG ステージ遷移と主要係数
4. LFO wave/speed/delay/pms/ams の解釈
5. feedback 挙動（OP4 系）
6. TL ramp 近似
7. limiter + declick
8. maxVoices（通常 8、低負荷モード 4）

数値再現方針:

1. 初期実装は double 主体
2. テーブル生成式と clamp 境界を JS と一致
3. NaN/Inf ガードを全出力経路へ実装

---

## 8. DX21.syx 取り込み仕様

WebDX21_DX21_SYX_Import_Spec.md を正とし、以下を必須一致とする。

1. 入力検証
- size >= 4104
- first = 0xF0
- last = 0xF7

2. Bulk 抽出
- offset 6
- length 4096
- 128 bytes x 32 voices

3. Operator 並び替え
- VMEM order: OP4, OP2, OP3, OP1
- UI order map: [3,1,2,0]

4. packed ビット展開
- algorithm, feedback, lfo sync, pms, ams

5. 編集時の VMEM 紐付け解除
- 生 VMEM 保持フラグを削除

---

## 9. UI 仕様（Standalone）

WebDX21_UI_Spec.md に従い、下記を必須再現とする。

## 9.1 ブロック構成

1. 上段: Patch パネル
2. 中段: Keyboard パネル
3. 下段: Operator 4パネル

## 9.2 主要操作

1. マウスホイールで値変更
2. 数値直接入力（Enter 確定 / Esc 取消）
3. Transpose ダブルクリックで 0
4. Pitch Wheel 自動センタ復帰
5. Preset 選択時の再描画同期

## 9.3 表示要素

1. LCD（16x2 ドット）
2. Algorithm 図
3. EG カーブ
4. Scope 表示（低負荷時は更新間引き）

---

## 10. VST3 仕様

## 10.1 プラグイン種別

1. Instrument（IS_SYNTH = true）
2. MIDI Input 必須
3. Audio Output: Stereo

## 10.2 入出力仕様

1. 入力
- MIDI Note On/Off
- Pitch Bend
- Mod Wheel(CC#1)
- Host Automation

2. 出力
- Stereo float32
- 内部は mono 合成後 L/R 同値を初期仕様とする
- 将来は真の stereo 拡張余地を残す

## 10.3 パラメータ公開方針

公開対象は UI すべての可変値。

1. Global
- masterVolume, transpose

2. Voice
- algorithm, feedback

3. LFO
- speed, delay, pmd, amd, pms, ams, wave, sync

4. Effects
- reverb, mix, tone, chorus, delay

5. Operator x4
- ratioIndex, detune, level, rateScale, levelScale, velocity
- ar, d1r, d1l, d2r, rr

要件:

1. 全パラメータは automation 対応
2. Host 表示名と単位を定義
3. 正規化レンジ 0..1 と実値変換を固定
4. 変更は sample-accurate で反映可能な構造

## 10.4 状態保存

1. プラグイン状態はバイナリチャンクで保存
2. 保存内容
- engine patch state
- selected voice index
- user patch 名
- UI 表示に必要な最小情報

3. 互換性
- バージョン番号を付与
- 将来のフィールド追加時に後方互換を維持

## 10.5 リアルタイム制約

1. processBlock 内で動的確保禁止
2. ファイル I/O 禁止
3. ログ出力を常時行わない
4. パラメータ平滑化は RT-safe 実装

---

## 11. Standalone 仕様

1. Web UI と同一レイアウト方針
2. VST3 と同一 Engine を利用
3. MIDI デバイス選択機能
4. Audio デバイス選択機能
5. DX21.syx 読み込み（起動時 + 手動再読み込み）
6. Factory/User Preset 管理

---

## 12. 将来の AU / CLAP 対応方針

## 12.1 設計要件

1. Engine 層を完全にフォーマット非依存に保つ
2. Parameter 定義を1か所に集約
3. State serializer を共通化
4. Host Adapter 層で VST3/AU/CLAP を分離

## 12.2 実装順序（推奨）

1. VST3 版完成
2. macOS Standalone
3. AU 追加
4. CLAP 追加

---

## 13. ビルド仕様（CMake + JUCE）

## 13.1 主要ターゲット

1. dx21_engine（static library）
2. DX21Native_Standalone
3. DX21Native_VST3
4. dx21_console_render

## 13.2 CMake 要件

1. C++17 以上
2. 警告レベル高
3. Debug/Release プリセット分離
4. Windows と macOS で同一 CMakeLists を維持

---

## 14. テスト仕様

## 14.1 Engine 単体

1. Envelope ステージ遷移
2. LFO 出力レンジ
3. Algorithm 配線整合
4. NaN/Inf 不在

## 14.2 Sysex

1. 正常 32 voice 読み込み
2. ヘッダ/サイズ不正エラー
3. operator order 変換検証

## 14.3 比較テスト

1. 代表パッチの 1ノートレンダを Web版と比較
2. RMS / peak / attack の差分監視
3. 許容誤差を閾値化

## 14.4 Plugin テスト

1. VST3 Validator 実行
2. DAW でのノート再生
3. Automation 記録/再生
4. 状態保存/復元

---

## 15. マイルストーン

1. M1: Engine 移植完了（Console でWAV出力）
2. M2: Sysex 読み込み統合
3. M3: Standalone UI 最小版
4. M4: Web同等 UI 完成
5. M5: VST3 初版（機能固定）
6. M6: クロスプラットフォーム整備（Windows/macOS）
7. M7: AU/CLAP 追加検討開始

---

## 16. リスクと対策

1. JS と C++ の数値差
- 対策: double 優先、比較レンダテスト

2. UI 実装コスト増大
- 対策: レイヤ分割、再利用コンポーネント化

3. Host 差異（DAWごとの挙動差）
- 対策: 主要DAWの受け入れテスト項目を定義

4. リアルタイム安全性
- 対策: AudioThread 禁止事項を静的レビュー対象化

---

## 17. 受け入れ条件

1. Standalone で Web UI 同等の画面構成・操作が可能
2. DX21.syx から 32 Voice を読み込み可能
3. VST3 で MIDI 演奏・Automation・State 保存が動作
4. 主要パッチの音色差が許容範囲に収まる
5. Windows ビルドが CI で再現可能
6. macOS ビルド手順が文書化済み

---

## 18. 参照ドキュメント

1. WebDX21_JS_Engine_Spec.md
2. WebDX21_DX21_SYX_Import_Spec.md
3. WebDX21_UI_Spec.md
