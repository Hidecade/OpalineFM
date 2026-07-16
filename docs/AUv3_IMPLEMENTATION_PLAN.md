# Opaline FM AUv3 実装計画

## 1. 目的

Opaline FMの既存iOSスタンドアロン版に、AUv3 Instrument Extensionを追加する。

iPhone／iPad版GarageBand、AUMなどのAUv3ホストからOpaline FMをソフトウェア音源として読み込み、ホストが送信するMIDIで演奏し、生成したステレオ音声をホストへ返せる状態を目標とする。

## 2. 基本方針

- `Source/Engine`以下の既存C++音源エンジンを再利用する。
- FM合成処理をSwiftで再実装しない。
- Mac版`PluginProcessor`のMIDI処理、パラメータ定義、状態保存処理を参考にする。
- 最初から全機能を移植せず、GarageBand／AUMから演奏できる最小AUv3を先に完成させる。
- 各Phaseでビルドと確認を行い、問題を解消してから次へ進む。
- 既存コードに問題を発見しても、現在のPhaseに不要な大規模変更は行わず、未解決事項へ記録する。

## 3. 変更してはならない動作

以下の既存ターゲットと機能を維持する。

- Windows Standalone
- Windows VST3
- macOS Standalone
- macOS VST3
- macOS Audio Unit
- iOS Standalone
- 既存SysEx音色バンクの読み書き
- 既存音色ライブラリと状態保存形式
- SINGLE／DUAL／SPLIT
- MIDIキーボード、画面鍵盤、Pitch Bend、Modulation、Sustain、Portamento

無関係なユーザー変更を上書きしない。作業開始時に必ず`git status`を確認する。

## 4. プロジェクト構成方針

```text
OpalineFMMobile
├── iOS Standalone App
│   ├── SwiftUI UI
│   ├── MobileSynthModel
│   ├── CoreMIDI
│   └── AVAudioEngine
│
├── AUv3 Instrument Extension
│   ├── AUAudioUnit
│   ├── AUParameterTree
│   ├── MIDI event processing
│   ├── AUv3 state persistence
│   └── AUv3 UI
│
└── Shared
    ├── Source/Engine
    ├── 音色データ構造
    ├── パラメータID／アドレス
    └── リアルタイム安全なC++ Bridge
```

AUv3インスタンスごとに独立した`OpalineEngine`を所有する。音源状態をグローバル変数や共有singletonに置かない。

## 5. リアルタイム音声処理の必須条件

AUv3の音声レンダースレッドでは、次の処理を禁止する。

- `mutex`、`CriticalSection`などの待機を伴うロック
- `new`、`malloc`、`vector::resize`などの動的メモリ確保
- ファイル読み書き
- XMLおよびSysExの解析
- Objective-C／Swiftオブジェクトの生成
- UI操作
- メインスレッドへの同期Dispatch
- ログの大量出力

必要な音声バッファは`allocateRenderResources`で、ホストの最大フレーム数に合わせて事前確保する。

UIやホストからのパラメータ変更は、次のいずれかの方式で音声エンジンへ渡す。

- `std::atomic`による単純値の受け渡し
- 固定長SPSCキュー
- ダブルバッファ化したパッチ状態

未初期化、リソース未確保、レンダー失敗時は出力をゼロクリアする。ホスト指定のサンプルレート、チャンネル数、最大フレーム数に追従する。

## 6. 実装Phase

### Phase A：AUv3 Extensionの雛形

- [ ] 作業開始前に`git status`を確認する
- [ ] 現在の`project.yml`とXcodeプロジェクト構成を確認する
- [ ] AUv3 Instrument Extensionターゲットを追加する
- [ ] Music Device／Instrumentとして登録する
- [ ] アプリ本体とExtensionで異なるBundle IDを設定する
- [ ] 本体とExtensionのバージョン／ビルド番号を揃える
- [ ] `project.yml`を正とし、XcodeGenで再生成可能な状態を維持する
- [ ] AUv3がホストのコンポーネント一覧へ登録される設定を追加する
- [ ] 最小の`AUAudioUnit`クラスを作成する
- [ ] 最小のAUv3 View Controllerを作成する
- [ ] アプリ本体と既存Mac／Windowsターゲットに影響がないことを確認する

#### Phase A 完了条件

- Xcodeでアプリ本体とAUv3 Extensionがビルドできる。
- AUv3ホストの音源一覧にOpaline FMが表示される。
- AUv3画面を開いてもクラッシュしない。
- この段階では無音でもよい。

### Phase B：C++レンダリングとMIDI

- [ ] AUv3専用のC++ Bridgeを追加する
- [ ] `Source/Engine`の`OpalineEngine`を再利用する
- [ ] AUv3インスタンスごとに独立したエンジンを作成する
- [ ] `allocateRenderResources`でエンジンをprepareする
- [ ] 最大フレーム数分のバッファを事前確保する
- [ ] `internalRenderBlock`からC++エンジンを呼び出す
- [ ] モノ出力とステレオ出力を安全に処理する
- [ ] MIDIイベントのsample offsetを保持する
- [ ] Note Onを実装する
- [ ] Note Offを実装する
- [ ] velocity 0のNote OnをNote Offとして処理する
- [ ] Pitch Bendを実装する
- [ ] CC1 Modulation Wheelを実装する
- [ ] CC64 Sustain Pedalを実装する
- [ ] CC65 Portamento Foot Switchを実装する
- [ ] CC120 All Sound Offを実装する
- [ ] CC123 All Notes Offを実装する
- [ ] レンダースレッドにロックと動的メモリ確保がないことを確認する

#### Phase B 完了条件

- GarageBandまたはAUMからMIDI演奏できる。
- 44.1kHzと48kHzでピッチと速度が正常である。
- Note OffおよびAll Notes Offで音が鳴りっぱなしにならない。
- 複数のバッファサイズでクラッシュしない。

### Phase C：AUParameterTreeと状態保存

Mac版`PluginProcessor`のパラメータ定義を基準にする。パラメータIDとアドレスは固定し、将来のバージョンでも変更しない。

#### 初期版で公開するパラメータ

- [ ] Master Volume
- [ ] Algorithm
- [ ] Feedback
- [ ] Transpose
- [ ] Pitch Bend Range
- [ ] Portamento
- [ ] LFO Wave
- [ ] LFO Speed
- [ ] LFO Delay
- [ ] PMD
- [ ] AMD
- [ ] PMS
- [ ] AMS
- [ ] OP1～OP4 Enabled
- [ ] OP1～OP4 Ratio
- [ ] OP1～OP4 Detune
- [ ] OP1～OP4 Level
- [ ] Reverb
- [ ] Delay
- [ ] Chorus
- [ ] Reverb Mix
- [ ] Delay Mix
- [ ] Tone
- [ ] Effects Enabled

#### 拡張パラメータ

- [ ] OP1～OP4 AR
- [ ] OP1～OP4 D1R
- [ ] OP1～OP4 D1L
- [ ] OP1～OP4 D2R
- [ ] OP1～OP4 RR
- [ ] OP1～OP4 Rate Scale
- [ ] OP1～OP4 Level Scale
- [ ] OP1～OP4 Velocity
- [ ] OP1～OP4 AM Enable
- [ ] PEG PR1～PR3
- [ ] PEG PL1～PL3
- [ ] Performance Mode
- [ ] Voice A／B
- [ ] DUAL Detune／Balance
- [ ] SPLIT Point

#### 状態保存

- [ ] `fullState`または`fullStateForDocument`を実装する
- [ ] 現在のパッチを保存する
- [ ] 音色名を保存する
- [ ] Voice A／BとPerformance Modeを保存する
- [ ] 音量、エフェクト、演奏設定を保存する
- [ ] ホストのプロジェクト再読込時に状態を復元する
- [ ] 不正または古い状態データでクラッシュしない
- [ ] 既存シリアライズ形式を再利用できる場合は再利用する

#### Phase C 完了条件

- GarageBand／AUMのプロジェクトを閉じて再度開いても音色が復元される。
- ホストから主要パラメータをオートメーションできる。
- 複数インスタンスの状態が混ざらない。

### Phase D：AUv3 UI

- [ ] AUv3用の音源操作モデルを作成する
- [ ] `MobileSynthModel`をAUv3内で直接使用しない
- [ ] UIと音源操作をプロトコルまたは共通インターフェースで分離する
- [ ] 音色名を表示する
- [ ] Algorithmを操作できる
- [ ] Feedbackを操作できる
- [ ] Master Volumeを操作できる
- [ ] 既存SwiftUI画面の再利用可能部分を抽出する
- [ ] AUv3の表示サイズ変更へ追従する
- [ ] AUv3内で`AVAudioEngine`を作成しない
- [ ] AUv3内でCoreMIDIクライアントを作成しない

#### Phase D 完了条件

- GarageBand／AUM内でUIが正常に表示される。
- UI操作とホストオートメーションの値が同期する。
- UIを閉じても音声処理が継続する。

### Phase E：テストと安定化

- [ ] Note On／Offのテスト
- [ ] velocity 0 Note Onのテスト
- [ ] Pitch Bendのテスト
- [ ] CC1、CC64、CC65のテスト
- [ ] CC120、CC123のテスト
- [ ] モノ／ステレオ出力のテスト
- [ ] 44.1kHz／48kHzのテスト
- [ ] 複数フレームサイズのテスト
- [ ] 状態保存／復元のテスト
- [ ] 複数インスタンス分離のテスト
- [ ] NaN／Infが発生しないことのテスト
- [ ] 無入力時に異常なDCやノイズが出ないことのテスト
- [ ] GarageBand実機確認
- [ ] AUM実機確認
- [ ] 複数AUv3同時起動確認
- [ ] バックグラウンド／フォアグラウンド復帰確認
- [ ] Bluetooth／イヤホン変更確認
- [ ] 連続演奏による負荷確認

#### Phase E 完了条件

- 自動テストがすべて成功する。
- 実機ホストで通常演奏、状態復元、複数起動が安定する。
- 既存ターゲットに回帰問題がない。

## 7. iOSスタンドアロン版の別途確認事項

AUv3実装と混同せず、必要に応じて別コミットで対応する。

- [ ] `AVAudioSession.interruptionNotification`
- [ ] `AVAudioSession.mediaServicesWereResetNotification`
- [ ] オーディオルート変更後の再初期化
- [ ] サンプルレート変更後の再prepare
- [ ] バックグラウンド／フォアグラウンド復帰
- [ ] `AVAudioEngine`再起動時のSource Node重複防止
- [ ] CoreMIDIのパケット境界をまたぐメッセージ処理

## 8. バージョンと配布上の注意

- iOSアプリ本体とAUv3 Extensionの`MARKETING_VERSION`を揃える。
- `CURRENT_PROJECT_VERSION`も揃える。
- Bundle IDは本体とExtensionで分ける。
- App Store提出に不要な権限を追加しない。
- マイクを使用しない場合、`NSMicrophoneUsageDescription`を追加しない。
- App Groupが必要かは状態・音色ライブラリの共有設計を決めてから判断する。
- 署名設定やApp Group変更が必要な場合は、実施前にユーザーへ確認する。
- リポジトリ内でCMake、iOS、Gitタグ、READMEのバージョンが不一致の場合、AUv3実装とは別課題として記録する。

## 9. Git運用

変更は以下の単位に分ける。

1. `AUv3 Phase A: add extension scaffold`
2. `AUv3 Phase B: add rendering and MIDI`
3. `AUv3 Phase C: add parameters and state`
4. `AUv3 Phase D: add extension UI`
5. `AUv3 Phase E: add tests and stabilization`

各Phaseで次を行う。

1. `git status`を確認する。
2. 対象Phaseだけを実装する。
3. ビルドとテストを行う。
4. 差分を確認する。
5. 本計画書のチェック項目と作業記録を更新する。
6. ユーザーの許可がある場合のみコミット・pushする。

Codexは明示的な指示なしにコミット、push、PR作成を行わない。

## 10. 進捗

- [ ] Phase A：AUv3 Extension雛形
- [ ] Phase B：C++レンダリングとMIDI
- [ ] Phase C：AUParameterTreeと状態保存
- [ ] Phase D：AUv3 UI
- [ ] Phase E：テストと安定化

## 11. ビルド・作業記録

| 日付 | Phase | 環境 | 結果 | コミット | 備考 |
| --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |

## 12. 未解決事項

- なし

## 13. 実機確認表

- [ ] GarageBandのAU音源一覧にOpaline FMが表示される
- [ ] AUMのAU音源一覧にOpaline FMが表示される
- [ ] MIDI鍵盤で演奏できる
- [ ] 画面鍵盤で演奏できる
- [ ] 44.1kHzで正常に再生できる
- [ ] 48kHzで正常に再生できる
- [ ] 音程と再生速度が正常である
- [ ] 音割れや周期的な音切れがない
- [ ] Sustainが正常に動作する
- [ ] Pitch Bendが正常に動作する
- [ ] プロジェクト再読込後に音色が復元される
- [ ] 複数インスタンスを同時に使用できる
- [ ] ホスト停止／再開後も演奏できる
- [ ] Bluetooth／イヤホン切替後も演奏できる

## 14. Codexへの実行指示テンプレート

### Phase A開始時

```text
docs/AUv3_IMPLEMENTATION_PLAN.mdを最初から最後まで読んでください。

現在のgit status、project.yml、Xcodeプロジェクト、既存Mac版PluginProcessor、iOS版Bridgeを確認してください。

今回は計画書のPhase Aだけを実装してください。Phase B以降には進まないでください。既存のWindows版、Mac版、VST3、macOS AU、iOSスタンドアロン版を壊さないでください。

project.ymlを正とし、必要ならXcodeGenでxcodeprojを再生成してください。作業後はビルド、差分確認、計画書のチェックボックスと作業記録の更新を行ってください。

コミット、push、PR作成は行わず、変更内容、ビルド結果、残課題、次に必要な実機確認を報告してください。
```

### 次Phase開始時

```text
docs/AUv3_IMPLEMENTATION_PLAN.mdを読み、現在の実装とgit statusを確認してください。

前Phaseの完了条件が満たされているか確認し、今回はPhase Xだけを実装してください。それ以降のPhaseには進まないでください。

作業後はビルドとテストを実行し、計画書のチェックボックス、ビルド・作業記録、未解決事項を更新してください。

コミット、push、PR作成は行わず、変更内容、検証結果、実機確認事項を報告してください。
```

## 15. Codexの完了報告形式

各Phase終了時に以下を報告する。

1. 追加・変更したファイル
2. 実装した機能
3. 採用した構造と判断理由
4. ビルド結果
5. テスト結果
6. 実機で確認すべき操作
7. 未完了事項
8. リアルタイム音声処理に残るリスク
9. 次Phaseへ進める状態かどうか
