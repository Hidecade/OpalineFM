#include "PluginProcessor.h"

#include "App/Dx21StateSerialization.h"
#include "PluginEditor.h"

#include <juce_core/juce_core.h>

namespace
{
double pitchWheelToUnitBend(const int pitchWheelValue)
{
    return dx21::clampDouble((static_cast<double>(pitchWheelValue) - 8192.0) / 8192.0, -1.0, 1.0);
}
} // namespace

Dx21NativeAudioProcessor::Dx21NativeAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    state.patch = dx21::normalizePatch(state.patch);
}

void Dx21NativeAudioProcessor::prepareToPlay(const double sampleRate, int)
{
    const juce::ScopedLock lock(engineLock);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    engine.prepare(currentSampleRate, dx21::kDefaultMaxVoices);
    applyStateToEngine();
}

void Dx21NativeAudioProcessor::releaseResources()
{
    const juce::ScopedLock lock(engineLock);
    engine.panic();
}

bool Dx21NativeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void Dx21NativeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const juce::ScopedLock lock(engineLock);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    auto midi = midiMessages.cbegin();
    const auto midiEnd = midiMessages.cend();
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        while (midi != midiEnd && (*midi).samplePosition <= sample)
        {
            handleMidiMessage((*midi).getMessage());
            ++midi;
        }

        const auto output = engine.renderSample();
        const float gain = state.masterVolume;
        if (buffer.getNumChannels() > 0)
            buffer.setSample(0, sample, output.left * gain);
        if (buffer.getNumChannels() > 1)
            buffer.setSample(1, sample, output.right * gain);
    }

    midiMessages.clear();
}

juce::AudioProcessorEditor* Dx21NativeAudioProcessor::createEditor()
{
    return new Dx21NativeAudioProcessorEditor(*this);
}

void Dx21NativeAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const juce::ScopedLock lock(engineLock);
    const auto tree = dx21app::synthStateToValueTree(state);
    if (auto xml = tree.createXml())
        copyXmlToBinary(*xml, destData);
}

void Dx21NativeAudioProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        const auto tree = juce::ValueTree::fromXml(*xml);
        const juce::ScopedLock lock(engineLock);
        state = dx21app::synthStateFromValueTree(tree, state);
        applyStateToEngine();
    }
}

dx21app::SynthState Dx21NativeAudioProcessor::getSynthState() const
{
    const juce::ScopedLock lock(engineLock);
    return state;
}

void Dx21NativeAudioProcessor::setSynthStateFromEditor(const dx21app::SynthState& newState)
{
    const juce::ScopedLock lock(engineLock);
    state = newState;
    applyStateToEngine();
}

void Dx21NativeAudioProcessor::setRenderModelFromEditor(const dx21::Dx21RenderModel newRenderModel)
{
    const juce::ScopedLock lock(engineLock);
    renderModel = newRenderModel;
    engine.setRenderModel(renderModel);
}

void Dx21NativeAudioProcessor::noteOnFromEditor(const int note, const int velocity)
{
    const juce::ScopedLock lock(engineLock);
    engine.noteOn(note, velocity);
}

void Dx21NativeAudioProcessor::noteOffFromEditor(const int note)
{
    const juce::ScopedLock lock(engineLock);
    engine.noteOff(note);
}

void Dx21NativeAudioProcessor::allNotesOffFromEditor()
{
    const juce::ScopedLock lock(engineLock);
    engine.panic();
}

void Dx21NativeAudioProcessor::handleMidiMessage(const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        engine.noteOn(message.getNoteNumber(), static_cast<int>(message.getVelocity() * 127.0f));
        return;
    }

    if (message.isNoteOff())
    {
        engine.noteOff(message.getNoteNumber());
        return;
    }

    if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        engine.panic();
        return;
    }

    if (message.isPitchWheel())
    {
        currentPitchBend = pitchWheelToUnitBend(message.getPitchWheelValue());
        engine.setPitchBend(currentPitchBend);
        return;
    }

    if (message.isController() && message.getControllerNumber() == 1)
    {
        currentModWheel = dx21::clampDouble(static_cast<double>(message.getControllerValue()) / 127.0, 0.0, 1.0);
        engine.setModWheel(currentModWheel);
    }
}

void Dx21NativeAudioProcessor::applyStateToEngine()
{
    state.patch = dx21::normalizePatch(state.patch);
    state.masterVolume = juce::jlimit(0.0f, 1.0f, state.masterVolume);
    engine.setRenderModel(renderModel);
    engine.setPatch(state.patch);
    engine.setPitchBend(currentPitchBend);
    engine.setModWheel(currentModWheel);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Dx21NativeAudioProcessor();
}
