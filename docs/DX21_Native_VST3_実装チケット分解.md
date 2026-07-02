# DX21 Native / VST3 実装チケット分解

## 1. 目的

本書は、[DX21_Native_VST3_移植仕様書.md](DX21_Native_VST3_移植仕様書.md) を実装可能な単位へ分解した開発チケット一覧である。

方針:

1. DSP コアを先に固定し、UI/Plugin は後段で接続
2. 各フェーズで「動く成果物」を残す
3. テスト可能性を常に優先

---

## 2. フェーズ一覧

1. M1: Engine コア移植（Console で発音検証）
2. M2: DX21.syx 読み込み統合
3. M3: Standalone UI 最小版
4. M4: Web同等 UI 完成
5. M5: VST3 初版完成
6. M6: クロスプラットフォーム整備
7. M7: AU/CLAP 拡張準備

---

## 3. M1 Engine コア移植

## T1-1 プロジェクト骨格作成

内容:

1. CMakeLists と CMakePresets の作成
2. JUCE の外部参照設定
3. `dx21_engine` static library ターゲット作成
4. Console ターゲット作成

完了条件:

1. Debug/Release の configure が通る
2. `dx21_engine` と console がビルド可能

依存:
- なし

## T1-2 基本型とテーブル実装

内容:

1. `Dx21Types`（Patch/Operator/Envelope/LFO）定義
2. DX21_RATIOS, ALGORITHMS, SINE_TABLE, EG_INC を実装
3. clamp/util 関数実装

完了条件:

1. テーブルサイズと要素が JS と一致
2. 単体テストで主要インデックスが一致

依存:
- T1-1

## T1-3 Envelope 実装

内容:

1. `Dx21Envelope` の stage 遷移実装
2. eg tick と attenuation[dB] 管理
3. AR/D1R/D2R/RR と rateScale 対応

完了条件:

1. off->attack->decay1->decay2->release が動作
2. D1L 変換式が仕様一致

依存:
- T1-2

## T1-4 Voice 実装

内容:

1. 4OP 演算と algorithm 依存再帰計算
2. feedback, detune, level scaling, TL ramp 実装
3. LFO/AM/PM 反映

完了条件:

1. 8アルゴリズムすべて動作
2. NaN/Inf ガードを実装

依存:
- T1-3

## T1-5 Engine 実装

内容:

1. voice pool（max voices）管理
2. noteOn/noteOff/pitchBend/modWheel/panic
3. renderBlock 実装（stereo 出力）
4. limiter + declick 実装

完了条件:

1. MIDIイベントで音が出る
2. 無音時の異常出力がない

依存:
- T1-4

## T1-6 Console WAV レンダラ

内容:

1. 指定パッチ + ノートでWAV書き出し
2. 簡易CLI（note, velocity, seconds, sampleRate）

完了条件:

1. WAV を再生して音が確認できる
2. CI でコマンド実行可能

依存:
- T1-5

---

## 4. M2 DX21.syx 読み込み統合

## T2-1 Sysex パーサ実装

内容:

1. Bulk検証（>=4104, F0, F7）
2. 32 voice x 128 bytes 抽出
3. Voice 名取得

完了条件:

1. 正常ファイルで32件取得
2. 不正ファイルで例外/エラー戻り

依存:
- T1-2

## T2-2 VMEM デコーダ実装

内容:

1. packed ビット展開
2. operator order map [3,1,2,0]
3. UI/Patch 構造への変換

完了条件:

1. algorithm/feedback/lfo/operator 値が一致
2. ampModEnable, detune, rateScale が一致

依存:
- T2-1

## T2-3 パッチマージ実装

内容:

1. factory preset と vmem preset のマージ
2. 編集時に VMEM 紐付け解除

完了条件:

1. preset選択時に vmem が反映される
2. 編集後に生VMEM保持フラグが外れる

依存:
- T2-2

## T2-4 Sysex テスト整備

内容:

1. 正常ケース、ヘッダ不正、サイズ不正
2. 代表 voice の値一致アサート

完了条件:

1. 自動テストで全ケース通過

依存:
- T2-2

---

## 5. M3 Standalone UI 最小版

## T3-1 MainWindow + Audio/MIDI 接続

内容:

1. JUCE Standalone 起動
2. AudioDeviceManager 接続
3. MIDI入力を engine に接続

完了条件:

1. キーボード入力/MIDI入力で発音

依存:
- T1-5

## T3-2 最小UI（演奏可能セット）

内容:

1. Power
2. Voice select
3. masterVolume / transpose
4. 画面鍵盤

完了条件:

1. GUIだけで音色選択と演奏が可能

依存:
- T3-1, T2-3

## T3-3 状態同期基盤

内容:

1. UI state -> PatchState 反映
2. audio thread 用 snapshot 転送

完了条件:

1. ノブ変更時に音切れなく反映

依存:
- T3-2

---

## 6. M4 Web同等 UI 完成

## T4-1 Patchパネル完成

内容:

1. LCD（16x2）
2. Algorithm diagram
3. LFO + Effects
4. 数値直接入力、wheel変更

完了条件:

1. Web版の上段構成と同等

依存:
- T3-3

## T4-2 Keyboardパネル完成

内容:

1. Pitch/Mod wheel 表示
2. Pitch 自動センタ復帰
3. 25鍵表示

完了条件:

1. Web版の中段操作感と同等

依存:
- T3-3

## T4-3 Operatorパネル完成

内容:

1. OP1-OP4 パネル
2. EGカーブ表示
3. 11ノブ/パネル
4. OP ON/OFF ボタン

完了条件:

1. Web版の下段構成と同等

依存:
- T3-3

## T4-4 低負荷表示モード

内容:

1. Scope更新間引き
2. 装飾軽量化フラグ

完了条件:

1. 低スペック環境で操作遅延が大幅悪化しない

依存:
- T4-1

## T4-5 UI 同等性テスト

内容:

1. 値域/ステップ一致チェック
2. 主要操作シナリオテスト

完了条件:

1. UI仕様の受け入れ条件を全通過

依存:
- T4-1, T4-2, T4-3

---

## 7. M5 VST3 初版完成

## T5-1 JUCE PluginProcessor 実装

内容:

1. `IS_SYNTH=true` 構成
2. `processBlock` で dx21_engine を呼び出し
3. MIDIイベント処理

完了条件:

1. DAW でノート再生可能

依存:
- T1-5

## T5-2 パラメータ公開

内容:

1. UI可変値すべてを APVTS へ定義
2. 0..1 正規化 <-> 実値変換
3. 表示名/単位定義

完了条件:

1. DAW で全パラメータ automation 可能

依存:
- T5-1, T4-5

## T5-3 状態保存/復元

内容:

1. getStateInformation / setStateInformation
2. バージョン付きチャンク設計

完了条件:

1. DAW セッション保存復元で値再現

依存:
- T5-2

## T5-4 RT-safe 監査

内容:

1. processBlock 内の動的確保除去
2. lock, file I/O, 重ログ排除

完了条件:

1. RT禁止事項チェックシート合格

依存:
- T5-1

## T5-5 VST3 検証

内容:

1. VST3 Validator 実行
2. 主要 DAW（最低2種）でスモークテスト

完了条件:

1. validator エラーなし
2. DAW動作で重大不具合なし

依存:
- T5-3, T5-4

---

## 8. M6 クロスプラットフォーム整備

## T6-1 macOS ビルド整備

内容:

1. CMake/JUCE で macOS ビルド
2. Standalone + VST3 生成確認

完了条件:

1. macOS で起動・発音確認

依存:
- T5-5

## T6-2 CI 整備

内容:

1. Windows CI
2. macOS CI
3. テストと artifact 出力

完了条件:

1. main push 時に自動ビルド成功

依存:
- T6-1

## T6-3 ドキュメント整備

内容:

1. ビルド手順
2. デバッグ手順
3. リリース手順

完了条件:

1. 新規環境で再現可能

依存:
- T6-2

---

## 9. M7 AU/CLAP 拡張準備

## T7-1 Adapter 層抽象化

内容:

1. Host 依存処理を分離
2. Parameter 定義共通化

完了条件:

1. VST3 実装を維持したまま Adapter 分離完了

依存:
- T5-5

## T7-2 AU 技術検証

内容:

1. AU ビルド可否確認
2. state/automation の差分調査

完了条件:

1. AU 対応見積りを確定

依存:
- T7-1

## T7-3 CLAP 技術検証

内容:

1. JUCE標準外対応方針選定
2. ラッパ/別実装の比較

完了条件:

1. CLAP 対応ロードマップ確定

依存:
- T7-1

---

## 10. 優先度と実行順

最優先:

1. T1-1〜T1-6
2. T2-1〜T2-4
3. T3-1〜T3-3
4. T4-1〜T4-5
5. T5-1〜T5-5

次点:

1. T6-1〜T6-3
2. T7-1〜T7-3

---

## 11. Definition of Done（共通）

各チケットの完了条件に加え、以下を満たすこと。

1. コードレビュー承認
2. テスト追加または既存テスト更新
3. Debug/Release ビルド通過
4. 仕様差分があれば docs 更新

---

## 12. リスク管理チケット（横断）

## R-1 音色差分モニタ

内容:

1. 代表パッチのレンダ比較
2. RMS/Peak/Attack 指標の継続監視

発火条件:
- Engine コア変更時

## R-2 パフォーマンス回帰

内容:

1. block処理時間の計測
2. voice数増加時のCPU監視

発火条件:
- DSP または UI 再描画変更時

## R-3 互換性回帰

内容:

1. 既存 state の復元テスト
2. パラメータ ID の互換性検証

発火条件:
- パラメータ定義変更時

---

## 13. 付録: 初回スプリント推奨（2週間）

1. Week1
- T1-1, T1-2, T1-3

2. Week2
- T1-4, T1-5, T1-6

スプリント完了条件:

1. Console で単音/和音レンダ可能
2. 代表パッチでクラッシュなく再生
3. 次スプリントで Sysex 統合に入れる状態
