#pragma once

#include "App/OpalineAppState.h"
#include "Engine/OpalineEngine.h"
#include "Engine/RealtimeAudioRecorder.h"
#include "Engine/RealtimeCommandQueue.h"
#include "Engine/RealtimeScopeBuffer.h"
#include "Engine/OpalineVoiceLibrary.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

class OpalineAudioProcessor final : public juce::AudioProcessor,
                                    private juce::AudioProcessorValueTreeState::Listener
{
public:
    OpalineAudioProcessor();
    ~OpalineAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    opalineapp::SynthState getSynthState() const;
    void setSynthStateFromEditor(const opalineapp::SynthState& newState);
    void setRenderModelFromEditor(opaline::OpalineRenderModel newRenderModel);
    void noteOnFromEditor(int note, int velocity);
    void noteOffFromEditor(int note);
    void allNotesOffFromEditor();
    void setPitchBendFromEditor(double value);
    void setModWheelFromEditor(double value);
    void setProgramNameFromEditor(const juce::String& name);
    double getCurrentPitchBend() const;
    double getCurrentModWheel() const;
    std::array<int, 128> getMidiUiVelocities() const;
    std::array<float, 4096> getScopeSamples() const;
    void setScopeCaptureEnabled(bool enabled) noexcept;
    void startWavRecording();
    void stopWavRecording();
    bool stopWavRecordingAndSaveToFile(const juce::File& file);

private:
    enum ParameterChangeBits : std::uint32_t
    {
        masterVolumeChanged = 1U << 0,
        pitchBendRangeChanged = 1U << 1,
        portamentoChanged = 1U << 2,
        modWheelRangesChanged = 1U << 3,
        effectsEnabledChanged = 1U << 4,
        patchChanged = 1U << 5,
        transposeChanged = 1U << 6,
        allParametersChanged = (1U << 7) - 1U
    };

    enum class RealtimeCommandType
    {
        noteOn,
        noteOff,
        panic,
        pitchBend,
        modWheel
    };

    struct RealtimeCommand
    {
        RealtimeCommandType type = RealtimeCommandType::panic;
        int value1 = 0;
        int value2 = 0;
        double normalizedValue = 0.0;
    };

    struct OperatorParameterPointers
    {
        std::atomic<float>* enabled = nullptr;
        std::atomic<float>* ampMod = nullptr;
        std::atomic<float>* ratio = nullptr;
        std::atomic<float>* detune = nullptr;
        std::atomic<float>* level = nullptr;
        std::atomic<float>* rateScale = nullptr;
        std::atomic<float>* levelScale = nullptr;
        std::atomic<float>* velocity = nullptr;
        std::atomic<float>* attackRate = nullptr;
        std::atomic<float>* decay1Rate = nullptr;
        std::atomic<float>* decay1Level = nullptr;
        std::atomic<float>* decay2Rate = nullptr;
        std::atomic<float>* releaseRate = nullptr;
    };

    struct ParameterPointers
    {
        std::atomic<float>* masterVolume = nullptr;
        std::atomic<float>* pitchBendRange = nullptr;
        std::atomic<float>* portamento = nullptr;
        std::atomic<float>* modWheelPitchRange = nullptr;
        std::atomic<float>* modWheelAmpRange = nullptr;
        std::atomic<float>* effectsEnabled = nullptr;
        std::atomic<float>* algorithm = nullptr;
        std::atomic<float>* feedback = nullptr;
        std::atomic<float>* transpose = nullptr;
        std::atomic<float>* lfoWave = nullptr;
        std::atomic<float>* lfoSync = nullptr;
        std::atomic<float>* lfoSpeed = nullptr;
        std::atomic<float>* lfoDelay = nullptr;
        std::atomic<float>* lfoPitchDepth = nullptr;
        std::atomic<float>* lfoAmpDepth = nullptr;
        std::atomic<float>* lfoPitchSensitivity = nullptr;
        std::atomic<float>* lfoAmpSensitivity = nullptr;
        std::atomic<float>* pegRate1 = nullptr;
        std::atomic<float>* pegRate2 = nullptr;
        std::atomic<float>* pegRate3 = nullptr;
        std::atomic<float>* pegLevel1 = nullptr;
        std::atomic<float>* pegLevel2 = nullptr;
        std::atomic<float>* pegLevel3 = nullptr;
        std::atomic<float>* effectReverb = nullptr;
        std::atomic<float>* effectMix = nullptr;
        std::atomic<float>* effectEchoMix = nullptr;
        std::atomic<float>* effectTone = nullptr;
        std::atomic<float>* effectChorus = nullptr;
        std::atomic<float>* effectDelay = nullptr;
        std::array<OperatorParameterPointers, opaline::kOperatorCount> operators {};
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleMidiMessage(const juce::MidiMessageMetadata& message) noexcept;
    void cacheParameterPointers();
    void applyAudioStateToEngine();
    void applyParametersToState(opalineapp::SynthState& targetState,
                                std::uint32_t changes = allParametersChanged) const noexcept;
    void applyParametersToAudioState(std::uint32_t changes);
    void applyParameterChangesToEngine(std::uint32_t changes);
    void publishStateUpdate();
    void enqueueRealtimeCommand(const RealtimeCommand& command) noexcept;
    void applyRealtimeCommands();
    void loadFactoryPrograms();
    opaline::OpalinePatch patchForVoiceIndex(int index, const opalineapp::SynthState& targetState) const;
    void performNoteOn(int note, int velocity);
    void performNoteOff(int note);
    void panicEngines();
    void setPitchBendOnEngines();
    void setModWheelOnEngines();
    juce::String factoryProgramName(int index) const;
    void syncParametersFromState();

    opaline::OpalineEngine engine;
    opaline::OpalineEngine performanceEngineB;
    opalineapp::SynthState state;
    opalineapp::SynthState audioState;
    juce::AudioProcessorValueTreeState parameters;
    ParameterPointers parameterPointers;
    std::atomic<std::uint32_t> dirtyParameterBits { allParametersChanged };
    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<double> currentPitchBend { 0.0 };
    std::atomic<double> currentModWheel { 0.0 };
    opalineapp::PerformanceMode preparedPerformanceMode = opalineapp::PerformanceMode::Single;
    juce::String currentProgramName { "Opaline FM" };
    std::vector<opaline::OpalinePatchWithMetadata> factoryPrograms;
    std::array<std::atomic<int>, 128> midiUiVelocities {};
    mutable opaline::RealtimeScopeBuffer scopeBuffer;
    mutable std::array<float, opaline::RealtimeScopeBuffer::historySize> scopeHistory {};
    mutable std::size_t scopeHistoryWriteIndex = 0;
    std::atomic<bool> scopeCaptureEnabled { false };
    opaline::RealtimeAudioRecorder wavRecorder;
    opaline::RealtimeCommandQueue<RealtimeCommand, 1024> realtimeCommands;
    std::atomic<bool> realtimeCommandOverflowed { false };
    opaline::RealtimeStateMailbox<opalineapp::SynthState> stateUpdates;
    mutable juce::CriticalSection stateLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpalineAudioProcessor)
};
