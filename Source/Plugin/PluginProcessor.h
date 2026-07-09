#pragma once

#include "App/OpalineAppState.h"
#include "Engine/OpalineEngine.h"
#include "Engine/OpalineVoiceLibrary.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <vector>

class OpalineAudioProcessor final : public juce::AudioProcessor
{
public:
    OpalineAudioProcessor();
    ~OpalineAudioProcessor() override = default;

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
    std::array<float, 256> getScopeSamples() const;
    void startWavRecording();
    void stopWavRecording();
    bool stopWavRecordingAndSaveToFile(const juce::File& file);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void handleMidiMessage(const juce::MidiMessage& message);
    void applyStateToEngine();
    void applyParametersToState();
    void loadFactoryPrograms();
    opaline::OpalinePatch patchForVoiceIndex(int index) const;
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
    opaline::OpalineRenderModel renderModel = opaline::OpalineRenderModel::TypeB;
    juce::AudioProcessorValueTreeState parameters;
    double currentSampleRate = 44100.0;
    double currentPitchBend = 0.0;
    double currentModWheel = 0.0;
    opalineapp::PerformanceMode preparedPerformanceMode = opalineapp::PerformanceMode::Single;
    juce::String currentProgramName { "Opaline FM" };
    std::vector<opaline::OpalinePatchWithMetadata> factoryPrograms;
    std::array<std::atomic<int>, 128> midiUiVelocities {};
    std::array<std::atomic<float>, 512> scopeSamples {};
    std::atomic<int> scopeWriteIndex { 0 };
    std::vector<float> wavRecordingInterleaved;
    double wavRecordingSampleRate = 44100.0;
    std::atomic<bool> wavRecording { false };
    mutable juce::CriticalSection wavRecordingLock;
    mutable juce::CriticalSection engineLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpalineAudioProcessor)
};
