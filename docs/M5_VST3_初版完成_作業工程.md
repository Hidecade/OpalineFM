# M5 VST3 初版完成 作業工程

## 目的

DX21Native を、Standalone だけでなく VST3 Instrument として DAW から読み込み、MIDI 演奏、主要パラメータ変更、状態保存/復元まで動作する初版へ進める。

初版では既存 Standalone UI の完全移植よりも、VST3 としての基本動作と安全な音声処理経路を優先する。

## 現状コード精査

### できていること

- `dx21_engine` は `Dx21Engine` として Standalone / Console / Tests から共通利用できる。
- `Dx21Engine::prepare()`, `setPatch()`, `noteOn()`, `noteOff()`, `setPitchBend()`, `setModWheel()`, `renderSample()` があり、VST3 `processBlock` から呼ぶための最低限のAPIはある。
- `Dx21AppState.h` に `SynthState` があり、StandaloneとVST3で共有できる状態構造がある。
- `Dx21StateSerialization.h` に `ValueTree` ベースの保存/復元処理があり、VST3の `getStateInformation()` / `setStateInformation()` に流用できる。
- CMake は JUCE を解決して `DX21Native_Standalone` を生成できている。

### 足りないこと

- VST3用の `juce_add_plugin()` ターゲットがまだない。
- `AudioProcessor` / `AudioProcessorEditor` がまだない。
- `MainComponent` が UI、AudioSource、AudioDevice、MIDI Input、状態保存、音源操作をすべて抱えているため、そのままVST3には使いにくい。
- Standalone音声処理は `engineMutex` を audio callback 内で lock している。VST3 `processBlock` では避けるべき。
- `Dx21Engine::renderSample()` 内で毎サンプル `voices.erase(remove_if(...))` を行っている。初版では動作可能だが、RT-safe監査ではブロック単位整理に寄せたい。
- パラメータ公開用の APVTS / `AudioProcessorValueTreeState` が未実装。
- Host automation と `Dx21Patch` の同期経路が未実装。

## M5 実装工程

### T5-0 VST3用の分離方針を固定

1. Standalone既存UIは壊さず維持する。
2. VST3初版はまず汎用JUCE editorまたは最小Editorで開始する。
3. `Dx21Engine` を直接 `AudioProcessor` から使う。
4. Standalone固有のAudioDevice/MIDIデバイス選択/ファイル保存UIはVST3へ持ち込まない。

完了条件:

- StandaloneとVST3の責務分離方針がコード上で破綻しない。

### T5-1 CMakeにVST3ターゲットを追加

1. `DX21_BUILD_PLUGIN` option を追加する。
2. JUCE検出時に `juce_add_plugin(DX21Native_Plugin ...)` を追加する。
3. 初版フォーマットは `VST3` のみ。
4. `IS_SYNTH TRUE`, MIDI input有効、audio output stereoにする。
5. `dx21_engine` と必要JUCE moduleへリンクする。

完了条件:

- `cmake --build --preset standalone-vs-debug --target DX21Native_Plugin_VST3` 相当で `.vst3` が生成される。

### T5-2 PluginProcessor最小実装

追加予定ファイル:

- `Source/Plugin/PluginProcessor.h`
- `Source/Plugin/PluginProcessor.cpp`
- `Source/Plugin/PluginEditor.h`
- `Source/Plugin/PluginEditor.cpp`

処理内容:

1. `prepareToPlay()` で `Dx21Engine::prepare()`。
2. `processBlock()` でMIDI Note On/Off、Pitch Bend、CC#1を処理。
3. `Dx21Engine::renderSample()` または `renderBlock()` でstereo出力。
4. 初期patchは `makeInitPatch()` または既存初期voice相当。
5. `releaseResources()` で `panic()`。

完了条件:

- DAW上でMIDIノートを鳴らすと音が出る。

### T5-3 パラメータ公開 初版

初版は全UIパラメータ一括ではなく、音色再現に必要な主要パラメータから開始する。

優先公開:

- Global: `masterVolume`, `transpose`, `engineModel`
- Voice: `algorithm`, `feedback`
- LFO: `wave`, `sync`, `speed`, `delay`, `pmd`, `amd`, `pms`, `ams`
- Pitch EG: `pr1`, `pr2`, `pr3`, `pl1`, `pl2`, `pl3`
- Effects: `reverb`, `mix`, `tone`, `chorus`, `delay`
- Operator x4: `ratioIndex`, `detune`, `level`, `rateScale`, `levelScale`, `velocity`, `ampModEnable`, `enabled`, `ar`, `d1r`, `d1l`, `d2r`, `rr`

実装方針:

1. `AudioProcessorValueTreeState` を使う。
2. 整数値は `NormalisableRange<float>` と丸めで扱う。
3. `processBlock()` 先頭でAPVTSから `Dx21Patch` を生成し、変更があった時だけ `engine.setPatch()` する。
4. 初版ではsample-accurate automationは目標外。block boundary反映で開始する。

完了条件:

- DAWから主要パラメータがautomation可能。

### T5-4 State保存/復元

1. `Dx21StateSerialization.h` の `synthStateToValueTree()` / `synthStateFromValueTree()` を流用する。
2. `getStateInformation()` はXMLまたはValueTreeバイナリを `MemoryBlock` へ保存する。
3. `setStateInformation()` は復元後、APVTSと内部 `Dx21Patch` へ反映する。
4. バージョン番号は既存 `version = 1` を継続。

完了条件:

- DAWセッション保存/再読み込みでパラメータと音色が戻る。

### T5-5 RT-safe監査と初版修正

初版で必ず見る項目:

- `processBlock()` 内でファイルI/Oなし。
- `processBlock()` 内でGUI操作なし。
- `processBlock()` 内でmutex lockなし。
- MIDIイベント処理で動的確保を避ける。
- `Dx21Engine::noteOn()` は `voices.reserve()` 済みだが、`std::vector` 操作があるため最大ボイス数を固定し、将来は固定配列化を検討する。
- `Dx21Engine::renderSample()` の毎サンプル `erase(remove_if())` は将来ブロック末尾整理へ移す。

完了条件:

- 重大なRT禁止処理が `processBlock()` に入っていない。

### T5-6 VST3検証

1. JUCE生成物の `.vst3` を確認。
2. 可能なら Steinberg VST3 Validator を実行。
3. 最低1つのDAWで以下を確認。

確認項目:

- プラグインスキャン成功。
- MIDI noteで発音。
- Note Offでrelease。
- Pitch Bend / Mod Wheel動作。
- パラメータ変更で音が変わる。
- セッション保存/復元。
- Standaloneビルドが壊れていない。

## 初版でやらないこと

- Standaloneと同等の完全UIをVST3 Editorへ移植すること。
- Factory bankブラウザの完全実装。
- sample-accurate automation完全対応。
- AU / CLAP対応。
- RT-safeの完全固定配列化。

## 推奨実装順

1. CMake `juce_add_plugin` 追加。
2. `PluginProcessor` / `PluginEditor` 最小実装。
3. MIDI演奏とaudio output確認。
4. APVTSで主要パラメータ公開。
5. State保存/復元。
6. RT-safe監査。
7. VST3検証。
