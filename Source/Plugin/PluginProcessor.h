#pragma once

#include "App/Dx21AppState.h"
#include "Engine/Dx21Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

class Dx21NativeAudioProcessor final : public juce::AudioProcessor
{
public:
    Dx21NativeAudioProcessor();
    ~Dx21NativeAudioProcessor() override = default;

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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    dx21app::SynthState getSynthState() const;
    void setSynthStateFromEditor(const dx21app::SynthState& newState);
    void setRenderModelFromEditor(dx21::Dx21RenderModel newRenderModel);
    void noteOnFromEditor(int note, int velocity);
    void noteOffFromEditor(int note);
    void allNotesOffFromEditor();

private:
    void handleMidiMessage(const juce::MidiMessage& message);
    void applyStateToEngine();

    dx21::Dx21Engine engine;
    dx21app::SynthState state;
    dx21::Dx21RenderModel renderModel = dx21::Dx21RenderModel::ChipHybrid;
    double currentSampleRate = 44100.0;
    double currentPitchBend = 0.0;
    double currentModWheel = 0.0;
    mutable juce::CriticalSection engineLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dx21NativeAudioProcessor)
};
