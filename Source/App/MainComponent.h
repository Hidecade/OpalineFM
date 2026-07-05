#pragma once

#include "App/Dx21AppState.h"
#include "Engine/Dx21Engine.h"
#include "Engine/Dx21Sysex.h"
#include "Engine/Dx21VoiceLibrary.h"

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
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Dx21LookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        Dx21LookAndFeel();
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
        void paint(juce::Graphics& g) override;

    private:
        void timerCallback() override { repaint(); }

        std::array<std::atomic<float>, 256> samples {};
        std::atomic<int> writeIndex { 0 };
    };

    class PitchEnvelopeGraphComponent final : public juce::Component
    {
    public:
        void setEnvelope(const dx21::Dx21PitchEnvelopeParams& params);
        void paint(juce::Graphics& g) override;

    private:
        dx21::Dx21PitchEnvelopeParams envelope;
    };

    class OperatorComponent final : public juce::Component,
                                    private juce::Slider::Listener,
                                    private juce::Button::Listener
    {
    public:
        using ChangeCallback = std::function<void(int, const dx21::Dx21Operator&)>;

        OperatorComponent(int operatorIndex, ChangeCallback callback);
        ~OperatorComponent() override;
        void setOperator(const dx21::Dx21Operator& newOperator);
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
        dx21::Dx21Operator op;
        juce::String role = "Carrier";
        Dx21LookAndFeel dx21LookAndFeel;
        juce::TextButton enableButton;
        juce::TextButton ampModButton { "AM" };
        juce::Label roleLabel;
        std::array<juce::Label, 6> opLabels;
        std::array<juce::Slider, 6> opSliders;
        std::array<juce::Label, 5> egLabels;
        std::array<juce::Slider, 5> egSliders;
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

    using PerformanceMode = dx21app::PerformanceMode;
    using PerformanceState = dx21app::PerformanceState;

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

    void loadFactoryVoices();
    void populateVoiceBankSelect();
    void refreshVoiceLists();
    void setCurrentVoiceBank(int bankIndex);
    void storeCurrentPatchToSelectedVoice();
    void loadVoiceBankFromFile(const juce::File& file);
    void loadVoiceLibraryFromFile(const juce::File& file);
    void saveCurrentVoiceBankToFile(const juce::File& file);
    void exportVoiceLibraryToFile(const juce::File& file);
    bool restoreSavedVoiceLibraryState();
    void saveVoiceLibraryState();
    void applySelectedVoice();
    void syncUiFromPatch();
    void updatePatchFromGlobalControls();
    void applyPatchToEngine();
    void applyPerformanceModeToEngines();
    dx21app::SynthState captureSynthState() const;
    void applySynthState(const dx21app::SynthState& state);
    void updatePerformanceFromControls();
    void refreshPerformanceControls();
    void refreshLcd();
    dx21::Dx21Patch patchForVoiceIndex(int index) const;
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
    void noteOn(int note, int velocity);
    void noteOff(int note);
    void performNoteOnNoLock(int note, int velocity);
    void performNoteOffNoLock(int note);
    void allNotesOff();
    bool isMidiUiNoteHeld(int note) const;
    int heldVelocityForNote(int note) const;
    void repaintKeyboardAsync();
    void syncPcKeyboardNotes();
    void timerCallback() override;

    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    void comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged) override;
    void buttonClicked(juce::Button* button) override;
    void sliderValueChanged(juce::Slider* slider) override;
    void sliderDragEnded(juce::Slider* slider) override;

    juce::Label titleLabel;
    juce::Label statusLabel;
    Dx21LookAndFeel dx21LookAndFeel;
    juce::ComboBox voiceSelect;
    juce::ComboBox voiceBankSelect;
    juce::ComboBox performanceModeSelect;
    juce::ComboBox voiceBSelect;
    juce::ComboBox audioOutputSelect;
    juce::ComboBox midiInputSelect;
    juce::ComboBox lfoWaveSelect;
    juce::TextButton powerButton { "OFF" };
    juce::TextButton loadVoiceBankButton { "Load" };
    juce::TextButton saveVoiceBankButton { "Save" };
    juce::TextButton exportVoiceLibraryButton { "Export" };
    juce::TextButton storeVoiceButton { "Store" };
    juce::ToggleButton lfoSyncButton { "Sync" };
    juce::Slider volumeSlider;
    juce::Slider transposeSlider;
    juce::Slider algorithmSlider;
    juce::Slider feedbackSlider;
    juce::Slider lfoSpeedSlider;
    juce::Slider lfoDelaySlider;
    juce::Slider lfoPitchDepthSlider;
    juce::Slider lfoAmpDepthSlider;
    juce::Slider lfoPitchSensitivitySlider;
    juce::Slider lfoAmpSensitivitySlider;
    juce::Slider pegRate1Slider;
    juce::Slider pegRate2Slider;
    juce::Slider pegRate3Slider;
    juce::Slider pegLevel1Slider;
    juce::Slider pegLevel2Slider;
    juce::Slider pegLevel3Slider;
    juce::Slider effectReverbSlider;
    juce::Slider effectMixSlider;
    juce::Slider effectToneSlider;
    juce::Slider effectChorusSlider;
    juce::Slider effectDelaySlider;
    juce::Slider pitchWheelSlider;
    juce::Slider modWheelSlider;
    juce::Slider dualDetuneSlider;
    SplitPointSlider splitPointSlider;
    juce::Label volumeLabel;
    juce::Label transposeLabel;
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
    juce::Label effectToneLabel;
    juce::Label effectChorusLabel;
    juce::Label effectDelayLabel;
    juce::Label pitchWheelLabel;
    juce::Label modWheelLabel;
    VoiceBadgeLabel voiceALabel;
    VoiceBadgeLabel voiceBLabel;
    juce::Label dualDetuneLabel;
    juce::Label splitPointLabel;
    LcdComponent lcd;
    AlgorithmComponent algorithmView;
    ScopeComponent scope;
    PitchEnvelopeGraphComponent pegGraph;
    KeyboardComponent keyboard;
    std::array<std::unique_ptr<OperatorComponent>, dx21::kOperatorCount> operatorPanels;

    dx21::Dx21Engine engine;
    dx21::Dx21Engine performanceEngineB;
    dx21::Dx21Patch currentPatch;
    PerformanceState performanceState;
    dx21::Dx21VoiceLibrary voiceLibrary;
    int currentVoiceBankIndex = 0;
    std::vector<dx21::Dx21PatchWithMetadata> factoryVoices;
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
    std::mutex engineMutex;
    std::array<bool, 128> pcKeyboardHeldNotes {};
    std::array<int, 128> pcKeyboardHeldVelocities {};

    float masterVolume = 0.8f;
    double audioSampleRate = 44100.0;
    double currentPitchBend = 0.0;
    double currentModWheel = 0.0;
    bool powerOn = false;
    bool audioStarted = false;
    bool syncingUi = false;
    juce::String midiStatus = "MIDI: not connected";
    juce::String audioStatus = "Audio: off";
};
