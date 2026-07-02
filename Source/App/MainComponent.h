#pragma once

#include "Engine/Dx21Engine.h"
#include "Engine/Dx21Sysex.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <mutex>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class MainComponent final : public juce::Component,
                            public juce::AudioSource,
                            private juce::MidiInputCallback,
                            private juce::ComboBox::Listener,
                            private juce::Button::Listener,
                            private juce::Slider::Listener
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
        void notify();

        int opIndex = 0;
        ChangeCallback onChange;
        dx21::Dx21Operator op;
        juce::String role = "Carrier";
        Dx21LookAndFeel dx21LookAndFeel;
        juce::TextButton enableButton;
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

    void loadFactoryVoices();
    void applySelectedVoice();
    void syncUiFromPatch();
    void updatePatchFromGlobalControls();
    void applyPatchToEngine();
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
    void noteOn(int note, int velocity);
    void noteOff(int note);
    void allNotesOff();
    bool isMidiUiNoteHeld(int note) const;
    void repaintKeyboardAsync();

    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    void comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged) override;
    void buttonClicked(juce::Button* button) override;
    void sliderValueChanged(juce::Slider* slider) override;
    void sliderDragEnded(juce::Slider* slider) override;

    juce::Label titleLabel;
    juce::Label statusLabel;
    Dx21LookAndFeel dx21LookAndFeel;
    juce::ComboBox voiceSelect;
    juce::ComboBox audioOutputSelect;
    juce::ComboBox midiInputSelect;
    juce::ComboBox lfoWaveSelect;
    juce::TextButton powerButton { "OFF" };
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
    juce::Slider effectReverbSlider;
    juce::Slider effectMixSlider;
    juce::Slider effectToneSlider;
    juce::Slider effectChorusSlider;
    juce::Slider effectDelaySlider;
    juce::Slider pitchWheelSlider;
    juce::Slider modWheelSlider;
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
    juce::Label effectReverbLabel;
    juce::Label effectMixLabel;
    juce::Label effectToneLabel;
    juce::Label effectChorusLabel;
    juce::Label effectDelayLabel;
    juce::Label pitchWheelLabel;
    juce::Label modWheelLabel;
    LcdComponent lcd;
    AlgorithmComponent algorithmView;
    ScopeComponent scope;
    KeyboardComponent keyboard;
    std::array<std::unique_ptr<OperatorComponent>, dx21::kOperatorCount> operatorPanels;

    dx21::Dx21Engine engine;
    dx21::Dx21Patch currentPatch;
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
    std::vector<std::unique_ptr<juce::MidiInput>> midiInputs;
    std::mutex engineMutex;

    float masterVolume = 0.8f;
    bool powerOn = false;
    bool audioStarted = false;
    bool syncingUi = false;
    juce::String midiStatus = "MIDI: not connected";
};
