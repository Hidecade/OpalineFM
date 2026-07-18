#pragma once

#include "App/OpalineAppState.h"
#include "Engine/OpalineEngine.h"
#include "Engine/RealtimeAudioRecorder.h"
#include "Engine/RealtimeCommandQueue.h"
#include "Engine/OpalineSysex.h"
#include "Engine/OpalineVoiceLibrary.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <mutex>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

class MainComponent final : public juce::Component,
                            public juce::AudioSource,
                            private juce::MidiInputCallback,
                            private juce::ComboBox::Listener,
                            private juce::Button::Listener,
                            private juce::Slider::Listener,
                            private juce::Timer
{
public:
    enum class HostMode
    {
        StandaloneApp,
        PluginEditor
    };

    using StateChangedCallback = std::function<void(const opalineapp::SynthState&)>;
    using RenderModelChangedCallback = std::function<void(opaline::OpalineRenderModel)>;
    using NoteOnCallback = std::function<void(int, int)>;
    using NoteOffCallback = std::function<void(int)>;
    using AllNotesOffCallback = std::function<void()>;
    using ControllerCallback = std::function<void(double)>;
    using ProgramNameChangedCallback = std::function<void(const juce::String&)>;
    using WavRecordingStartCallback = std::function<void()>;
    using WavRecordingStopCallback = std::function<void()>;
    using WavRecordingSaveCallback = std::function<bool(const juce::File&)>;

    explicit MainComponent(HostMode mode = HostMode::StandaloneApp,
                           bool allowPluginPcKeyboard = false);
    ~MainComponent() override;

    opalineapp::SynthState captureSynthState() const;
    void applySynthState(const opalineapp::SynthState& state, bool resetPatchToSelectedVoice = false);
    opaline::OpalineRenderModel currentRenderModel() const;
    void setStateChangedCallback(StateChangedCallback callback);
    void setRenderModelChangedCallback(RenderModelChangedCallback callback);
    void setNoteOnCallback(NoteOnCallback callback);
    void setNoteOffCallback(NoteOffCallback callback);
    void setAllNotesOffCallback(AllNotesOffCallback callback);
    void setPitchBendCallback(ControllerCallback callback);
    void setModWheelCallback(ControllerCallback callback);
    void setProgramNameChangedCallback(ProgramNameChangedCallback callback);
    void setWavRecordingCallbacks(WavRecordingStartCallback startCallback,
                                  WavRecordingStopCallback stopCallback,
                                  WavRecordingSaveCallback saveCallback);
    juce::String currentProgramName() const;
    void setExternalMidiNoteState(const std::array<int, 128>& velocities);
    void setExternalControllerState(double pitchBend, double modWheel);
    void setExternalScopeSamples(const std::array<float, 4096>& samples, double sampleRate);

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    class OpalineLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        OpalineLookAndFeel();
        void drawRotarySlider(juce::Graphics& g,
                              int x,
                              int y,
                              int width,
                              int height,
                              float sliderPos,
                              float rotaryStartAngle,
                              float rotaryEndAngle,
                              juce::Slider& slider) override;
        void drawLinearSlider(juce::Graphics& g,
                              int x,
                              int y,
                              int width,
                              int height,
                              float sliderPos,
                              float minSliderPos,
                              float maxSliderPos,
                              const juce::Slider::SliderStyle style,
                              juce::Slider& slider) override;
        juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override;
        void drawButtonBackground(juce::Graphics& g,
                                  juce::Button& button,
                                  const juce::Colour& backgroundColour,
                                  bool shouldDrawButtonAsHighlighted,
                                  bool shouldDrawButtonAsDown) override;
        void drawButtonText(juce::Graphics& g,
                            juce::TextButton& button,
                            bool shouldDrawButtonAsHighlighted,
                            bool shouldDrawButtonAsDown) override;
        void drawToggleButton(juce::Graphics& g,
                              juce::ToggleButton& button,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;
        juce::Label* createSliderTextBox(juce::Slider& slider) override;
        juce::Font getComboBoxFont(juce::ComboBox& box) override;
        juce::Font getPopupMenuFont() override;
    };

    class LcdComponent final : public juce::Component
    {
    public:
        void setLines(juce::String topLine, juce::String bottomLine);
        void paint(juce::Graphics& g) override;

    private:
        juce::String line1 = "PLAY SINGLE";
        juce::String line2 = "A01 INIT VOICE";
    };

    class VoiceBadgeLabel final : public juce::Label
    {
    public:
        void paint(juce::Graphics& g) override;
    };

    class AlgorithmComponent final : public juce::Component
    {
    public:
        void setAlgorithm(int newAlgorithm, int newFeedback);
        void paint(juce::Graphics& g) override;

    private:
        int algorithm = 1;
        int feedback = 0;
    };

    class ScopeComponent final : public juce::Component,
                                 private juce::Timer
    {
    public:
        ScopeComponent();
        void pushSample(float sample);
        void setSamples(const std::array<float, 4096>& newSamples);
        void setTrigger(int midiNote, double sampleRate);
        void paint(juce::Graphics& g) override;

    private:
        void timerCallback() override { repaint(); }

        std::array<std::atomic<float>, 4096> samples {};
        std::atomic<int> writeIndex { 0 };
        std::atomic<int> triggerMidiNote { -1 };
        std::atomic<double> scopeSampleRate { 44100.0 };
        std::array<float, 256> smoothedDisplaySamples {};
        int smoothedDisplayNote = -1;
        bool hasSmoothedDisplay = false;
    };

    class PitchEnvelopeGraphComponent final : public juce::Component
    {
    public:
        void setEnvelope(const opaline::OpalinePitchEnvelopeParams& params);
        void paint(juce::Graphics& g) override;

    private:
        opaline::OpalinePitchEnvelopeParams envelope;
    };

    class StepWheelSlider final : public juce::Slider
    {
    public:
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
        {
            const float delta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : wheel.deltaX;
            if (delta == 0.0f)
                return;

            const double interval = getInterval() > 0.0 ? getInterval() : 1.0;
            const double direction = delta > 0.0f ? 1.0 : -1.0;
            setValue(juce::jlimit(getMinimum(), getMaximum(), getValue() + direction * interval),
                     juce::sendNotificationSync);
        }
    };

    class OperatorComponent final : public juce::Component,
                                    private juce::Slider::Listener,
                                    private juce::Button::Listener
    {
    public:
        using ChangeCallback = std::function<void(int, const opaline::OpalineOperator&)>;

        OperatorComponent(int operatorIndex, ChangeCallback callback);
        ~OperatorComponent() override;
        void setOperator(const opaline::OpalineOperator& newOperator);
        void setRole(juce::String newRole);
        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        void sliderValueChanged(juce::Slider* slider) override;
        void buttonClicked(juce::Button* button) override;
        void setupSlider(juce::Slider& slider, double min, double max, double step, double value);
        void addLabeledSlider(juce::Label& label, juce::Slider& slider, const juce::String& text);
        void updateEnableButtonStyle();
        void updateAmpModButtonStyle();
        void notify();

        int opIndex = 0;
        ChangeCallback onChange;
        opaline::OpalineOperator op;
        juce::String role = "Carrier";
        OpalineLookAndFeel opalineLookAndFeel;
        juce::TextButton enableButton;
        juce::TextButton ampModButton { "AM" };
        juce::Label roleLabel;
        std::array<juce::Label, 6> opLabels;
        std::array<StepWheelSlider, 6> opSliders;
        std::array<juce::Label, 5> egLabels;
        std::array<StepWheelSlider, 5> egSliders;
    };

    class KeyboardComponent final : public juce::Component
    {
    public:
        explicit KeyboardComponent(MainComponent& owner);
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;
        void mouseExit(const juce::MouseEvent& event) override;

    private:
        int noteForPosition(juce::Point<int> position) const;
        void updateHeldNote(int note);

        MainComponent& owner;
        int heldNote = -1;
    };

    using PerformanceMode = opalineapp::PerformanceMode;
    using PerformanceState = opalineapp::PerformanceState;

    enum class RealtimeCommandType
    {
        noteOn,
        noteOff,
        panic,
        pitchBend,
        modWheel,
        sustainPedal,
        portamentoFootSwitch
    };

    struct RealtimeCommand
    {
        RealtimeCommandType type {};
        int value1 = 0;
        int value2 = 0;
        double normalizedValue = 0.0;
    };

    struct EngineState
    {
        opaline::OpalinePatch patchA;
        opaline::OpalinePatch patchB;
        PerformanceState performance;
        opaline::OpalineRenderModel renderModel = opaline::OpalineRenderModel::TypeB;
        double pitchBend = 0.0;
        double modWheel = 0.0;
        int pitchBendRange = 2;
        int portamento = 0;
        int modWheelPitchRange = 99;
        int modWheelAmpRange = 0;
        bool effectsEnabled = true;
    };

    class SplitPointSlider final : public juce::Slider
    {
    public:
        juce::String getTextFromValue(double value) override
        {
            return noteName(static_cast<int>(std::round(value)));
        }

        double getValueFromText(const juce::String& text) override
        {
            const auto trimmed = text.trim().toUpperCase();
            for (int note = 0; note < 128; ++note)
            {
                if (noteName(note) == trimmed)
                    return static_cast<double>(note);
            }

            return juce::jlimit(0.0, 127.0, trimmed.getDoubleValue());
        }

        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
        {
            const float delta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : wheel.deltaX;
            if (delta == 0.0f)
                return;

            setValue(juce::jlimit(getMinimum(), getMaximum(), getValue() + (delta > 0.0f ? 1.0 : -1.0)),
                     juce::sendNotificationSync);
        }

    private:
        static juce::String noteName(int note)
        {
            static constexpr std::array<const char*, 12> names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const int safeNote = juce::jlimit(0, 127, note);
            return juce::String(names[static_cast<std::size_t>(safeNote % 12)]) + juce::String(safeNote / 12 - 1);
        }
    };

    class UnitWheelSlider final : public juce::Slider
    {
    public:
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
        {
            const float delta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : wheel.deltaX;
            if (delta == 0.0f)
                return;

            const double step = getMaximum() <= 1.0 ? 0.01 : 1.0;
            const double direction = delta > 0.0f ? 1.0 : -1.0;
            const double nextValue = juce::jlimit(getMinimum(), getMaximum(), getValue() + direction * step);
            setValue(nextValue, juce::sendNotificationSync);
        }
    };

    void loadFactoryVoices();
    void populateVoiceBankSelect();
    void refreshVoiceLists();
    void setCurrentVoiceBank(int bankIndex);
    void storeCurrentPatchToSelectedVoice();
    void loadVoiceBankFromFile(const juce::File& file);
    void loadVoiceLibraryFromFile(const juce::File& file);
    void loadSingleVoiceFromFile(const juce::File& file);
    void applyImportedSingleVoice(const opaline::OpalinePatchWithMetadata& voice, const juce::String& status);
    void saveCurrentVoiceBankToFile(const juce::File& file);
    void saveSingleVoiceToFile(const juce::File& file);
    void exportVoiceLibraryToFile(const juce::File& file);
    bool restoreSavedVoiceLibraryState();
    void saveVoiceLibraryState();
    void applySelectedVoice(int selectedId = 0);
    void syncUiFromPatch();
    void updatePatchFromGlobalControls();
    void applyPatchToEngine(bool updateUi = true, bool notifyState = true);
    EngineState captureEngineState() const;
    void publishEngineState();
    void applyEngineStateNoLock(const EngineState& state);
    void refreshEngineModelButton();
    void emitSynthStateChanged();
    void updatePerformanceFromControls();
    void refreshPerformanceControls();
    void refreshLcd();
    void refreshCurrentVoiceNameDisplay();
    void emitProgramNameChanged();
    opaline::OpalinePatch patchForVoiceIndex(int index) const;
    juce::String currentVoiceText() const;
    juce::String performanceVoiceText(int index) const;
    void setupSlider(juce::Slider& slider, double min, double max, double step, double value, juce::Slider::SliderStyle style);
    void setupLabel(juce::Label& label, const juce::String& text);
    void setupComboBox(juce::ComboBox& comboBox);
    void populateAudioOutputSelect();
    void populateMidiInputSelect();
    void refreshAlgorithmAndRoles();
    void refreshStatus();
    bool ensureAudioStarted();
    bool startPlayback();
    void restartAudioOutput();
    void connectMidiInputs();
    void restoreAudioOutputSelection();
    void saveAudioOutputSelection() const;
    void restoreMidiInputSelection();
    void saveMidiInputSelection() const;
    void startWavRecording();
    void stopWavRecordingAndChooseFile();
    void writeWavRecordingToFile(const juce::File& file);
    void noteOn(int note, int velocity);
    void noteOff(int note);
    void performNoteOnNoLock(int note, int velocity);
    void performNoteOffNoLock(int note);
    void enqueueRealtimeCommand(const RealtimeCommand& command);
    void applyRealtimeCommandsNoLock();
    void allNotesOff();
    bool isMidiUiNoteHeld(int note) const;
    int heldVelocityForNote(int note) const;
    void repaintKeyboardAsync();
    bool pcKeyboardInputAllowed() const;
    void syncPcKeyboardNotes();
    void timerCallback() override;

    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    void comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged) override;
    void buttonClicked(juce::Button* button) override;
    void sliderValueChanged(juce::Slider* slider) override;
    void sliderDragEnded(juce::Slider* slider) override;

    juce::Label titleLabel;
    juce::Label statusLabel;
    OpalineLookAndFeel opalineLookAndFeel;
    juce::ComboBox voiceSelect;
    juce::ComboBox voiceBankSelect;
    juce::ComboBox performanceModeSelect;
    juce::ComboBox voiceBSelect;
    juce::TextButton voiceAPreviousButton { "<" };
    juce::TextButton voiceANextButton { ">" };
    juce::TextButton voiceBPreviousButton { "<" };
    juce::TextButton voiceBNextButton { ">" };
    juce::TextButton polyMonoAButton { "MONO" };
    juce::TextButton polyMonoBButton { "MONO" };
    juce::TextButton portamentoModeAButton { "PORTA" };
    juce::TextButton portamentoModeBButton { "PORTA" };
    juce::ComboBox audioOutputSelect;
    juce::ComboBox midiInputSelect;
    juce::ComboBox lfoWaveSelect;
    juce::TextButton powerButton { "OFF" };
    juce::TextButton wavRecordButton { "WAV" };
    juce::TextButton effectsEnableButton { "EFFECT" };
    juce::TextButton loadVoiceBankButton { "Load" };
    juce::TextButton saveVoiceBankButton { "Save" };
    juce::TextButton exportVoiceLibraryButton { "Export" };
    juce::TextButton loadSingleVoiceButton { "LOAD" };
    juce::TextButton saveSingleVoiceButton { "SAVE" };
    juce::TextButton initVoiceButton { "INIT" };
    juce::TextButton storeVoiceButton { "STORE" };
    juce::TextButton copyVoiceButton { "COPY" };
    juce::TextButton pasteVoiceButton { "PASTE" };
    juce::TextButton engineModelButton { "TYPE B" };
    juce::ToggleButton lfoSyncButton { "Sync" };
    UnitWheelSlider volumeSlider;
    UnitWheelSlider transposeSlider;
    UnitWheelSlider balanceSlider;
    StepWheelSlider algorithmSlider;
    StepWheelSlider feedbackSlider;
    StepWheelSlider lfoSpeedSlider;
    StepWheelSlider lfoDelaySlider;
    StepWheelSlider lfoPitchDepthSlider;
    StepWheelSlider lfoAmpDepthSlider;
    StepWheelSlider lfoPitchSensitivitySlider;
    StepWheelSlider lfoAmpSensitivitySlider;
    StepWheelSlider pegRate1Slider;
    StepWheelSlider pegRate2Slider;
    StepWheelSlider pegRate3Slider;
    StepWheelSlider pegLevel1Slider;
    StepWheelSlider pegLevel2Slider;
    StepWheelSlider pegLevel3Slider;
    StepWheelSlider effectReverbSlider;
    StepWheelSlider effectMixSlider;
    StepWheelSlider effectEchoMixSlider;
    StepWheelSlider effectToneSlider;
    StepWheelSlider effectChorusSlider;
    StepWheelSlider effectDelaySlider;
    juce::Slider pitchWheelSlider;
    juce::Slider modWheelSlider;
    StepWheelSlider pitchBendRangeSlider;
    StepWheelSlider portamentoSlider;
    StepWheelSlider modWheelPitchRangeSlider;
    StepWheelSlider modWheelAmpRangeSlider;
    UnitWheelSlider dualDetuneSlider;
    SplitPointSlider splitPointSlider;
    juce::Label volumeLabel;
    juce::Label transposeLabel;
    juce::Label balanceLabel;
    juce::Label algorithmGraphLabel;
    juce::Label pegGraphLabel;
    juce::Label algorithmLabel;
    juce::Label feedbackLabel;
    juce::Label lfoWaveLabel;
    juce::Label lfoSpeedLabel;
    juce::Label lfoDelayLabel;
    juce::Label lfoPitchDepthLabel;
    juce::Label lfoAmpDepthLabel;
    juce::Label lfoPitchSensitivityLabel;
    juce::Label lfoAmpSensitivityLabel;
    juce::Label pegLeftSeparator;
    juce::Label lfoLeftSeparator;
    juce::Label lfoRightSeparator;
    juce::Label pegTitleLabel;
    juce::Label pegRate1Label;
    juce::Label pegRate2Label;
    juce::Label pegRate3Label;
    juce::Label pegLevel1Label;
    juce::Label pegLevel2Label;
    juce::Label pegLevel3Label;
    juce::Label effectReverbLabel;
    juce::Label effectMixLabel;
    juce::Label effectEchoMixLabel;
    juce::Label effectToneLabel;
    juce::Label effectChorusLabel;
    juce::Label effectDelayLabel;
    juce::Label pitchWheelLabel;
    juce::Label modWheelLabel;
    juce::Label pitchBendRangeLabel;
    juce::Label portamentoLabel;
    juce::Label modWheelPitchRangeLabel;
    juce::Label modWheelAmpRangeLabel;
    VoiceBadgeLabel voiceALabel;
    VoiceBadgeLabel voiceBLabel;
    juce::Label dualDetuneLabel;
    juce::Label splitPointLabel;
    LcdComponent lcd;
    AlgorithmComponent algorithmView;
    ScopeComponent scope;
    PitchEnvelopeGraphComponent pegGraph;
    KeyboardComponent keyboard;
    std::array<std::unique_ptr<OperatorComponent>, opaline::kOperatorCount> operatorPanels;

    opaline::OpalineEngine engine;
    opaline::OpalineEngine performanceEngineB;
    opaline::OpalinePatch currentPatch;
    opaline::OpalinePatchWithMetadata copiedVoice;
    juce::String currentVoiceName { "INIT VOICE" };
    PerformanceState performanceState;
    opaline::OpalineVoiceLibrary voiceLibrary;
    int currentVoiceBankIndex = 0;
    std::vector<opaline::OpalinePatchWithMetadata> factoryVoices;
    struct AudioOutputChoice
    {
        juce::String typeName;
        juce::String deviceName;
    };

    std::vector<AudioOutputChoice> audioOutputChoices;
    juce::Array<juce::MidiDeviceInfo> midiInputDevices;
    std::unique_ptr<juce::AudioDeviceManager> audioDeviceManager;
    juce::AudioSourcePlayer audioSourcePlayer;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::vector<std::unique_ptr<juce::MidiInput>> midiInputs;
    opaline::RealtimeCommandQueue<RealtimeCommand, 1024> realtimeCommands;
    std::atomic<bool> realtimeCommandOverflowed { false };
    opaline::RealtimeStateMailbox<EngineState> engineStateUpdates;
    EngineState audioEngineState;
    opaline::RealtimeAudioRecorder wavRecorder;
    std::atomic<bool> wavRecording { false };
    std::array<bool, 128> pcKeyboardHeldNotes {};
    std::array<int, 128> pcKeyboardHeldVelocities {};

    float masterVolume = 0.65f;
    std::atomic<float> audioMasterVolume { 0.65f };
    double audioSampleRate = 44100.0;
    int retainedScopeTriggerNote = -1;
    std::atomic<double> currentPitchBend { 0.0 };
    std::atomic<double> currentModWheel { 0.0 };
    int pitchBendRange = 2;
    int portamento = 0;
    int modWheelPitchRange = 99;
    int modWheelAmpRange = 0;
    bool effectsEnabled = true;
    std::atomic<bool> powerOn { false };
    bool audioStarted = false;
    bool chipRenderModel = true;
    bool syncingUi = false;
    bool suppressStateCallback = false;
    bool suppressVoiceSelectionCallback = false;
    bool hasCopiedPatch = false;
    HostMode hostMode = HostMode::StandaloneApp;
    bool pluginPcKeyboardAllowed = false;
    StateChangedCallback onStateChanged;
    RenderModelChangedCallback onRenderModelChanged;
    NoteOnCallback onNoteOn;
    NoteOffCallback onNoteOff;
    AllNotesOffCallback onAllNotesOff;
    ControllerCallback onPitchBend;
    ControllerCallback onModWheel;
    ProgramNameChangedCallback onProgramNameChanged;
    WavRecordingStartCallback onExternalWavRecordingStart;
    WavRecordingStopCallback onExternalWavRecordingStop;
    WavRecordingSaveCallback onExternalWavRecordingSave;
    juce::String midiStatus = "MIDI: not connected";
    juce::String audioStatus = "Audio: off";
};
