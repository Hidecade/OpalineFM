#pragma once

#include "App/MainComponent.h"

#include <juce_audio_processors/juce_audio_processors.h>

class Dx21NativeAudioProcessor;

class Dx21NativeAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit Dx21NativeAudioProcessorEditor(Dx21NativeAudioProcessor& processor);
    ~Dx21NativeAudioProcessorEditor() override = default;

    void resized() override;

private:
    Dx21NativeAudioProcessor& audioProcessor;
    MainComponent mainComponent { MainComponent::HostMode::PluginEditor };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dx21NativeAudioProcessorEditor)
};
