#pragma once

#include "App/MainComponent.h"

#include <juce_audio_processors/juce_audio_processors.h>

class OpalineAudioProcessor;

class OpalineAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit OpalineAudioProcessorEditor(OpalineAudioProcessor& processor);
    ~OpalineAudioProcessorEditor() override;

    void resized() override;

private:
    void timerCallback() override;

    OpalineAudioProcessor& audioProcessor;
    MainComponent mainComponent { MainComponent::HostMode::PluginEditor };
    int externalStateSyncHoldoffFrames = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpalineAudioProcessorEditor)
};
