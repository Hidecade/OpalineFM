#include "PluginProcessor.h"

#include "App/OpalineStateSerialization.h"
#include "PluginEditor.h"

#include <juce_core/juce_core.h>

namespace
{
namespace param_ids
{
constexpr const char* masterVolume = "masterVolume";
constexpr const char* renderModel = "renderModel";
constexpr const char* algorithm = "algorithm";
constexpr const char* feedback = "feedback";
constexpr const char* transpose = "transpose";
constexpr const char* lfoWave = "lfoWave";
constexpr const char* lfoSync = "lfoSync";
constexpr const char* lfoSpeed = "lfoSpeed";
constexpr const char* lfoDelay = "lfoDelay";
constexpr const char* lfoPitchDepth = "lfoPitchDepth";
constexpr const char* lfoAmpDepth = "lfoAmpDepth";
constexpr const char* lfoPitchSensitivity = "lfoPitchSensitivity";
constexpr const char* lfoAmpSensitivity = "lfoAmpSensitivity";
constexpr const char* pegRate1 = "pegRate1";
constexpr const char* pegRate2 = "pegRate2";
constexpr const char* pegRate3 = "pegRate3";
constexpr const char* pegLevel1 = "pegLevel1";
constexpr const char* pegLevel2 = "pegLevel2";
constexpr const char* pegLevel3 = "pegLevel3";
constexpr const char* effectReverb = "effectReverb";
constexpr const char* effectMix = "effectMix";
constexpr const char* effectTone = "effectTone";
constexpr const char* effectChorus = "effectChorus";
constexpr const char* effectDelay = "effectDelay";
} // namespace param_ids

double pitchWheelToUnitBend(const int pitchWheelValue)
{
    return opaline::clampDouble((static_cast<double>(pitchWheelValue) - 8192.0) / 8192.0, -1.0, 1.0);
}

int parameterInt(juce::AudioProcessorValueTreeState& parameters, const juce::StringRef id, const int fallback)
{
    if (auto* value = parameters.getRawParameterValue(id))
        return static_cast<int>(std::round(value->load()));

    return fallback;
}

float parameterFloat(juce::AudioProcessorValueTreeState& parameters, const juce::StringRef id, const float fallback)
{
    if (auto* value = parameters.getRawParameterValue(id))
        return value->load();

    return fallback;
}

bool parameterBool(juce::AudioProcessorValueTreeState& parameters, const juce::StringRef id, const bool fallback)
{
    return parameterInt(parameters, id, fallback ? 1 : 0) != 0;
}

juce::String opParamId(const int opIndex, const char* suffix)
{
    return "op" + juce::String(opIndex + 1) + suffix;
}

juce::String opParamName(const int opIndex, const char* name)
{
    return "OP" + juce::String(opIndex + 1) + " " + name;
}

void setApvtsParameter(juce::AudioProcessorValueTreeState& parameters, const juce::StringRef id, const float value)
{
    if (auto* parameter = parameters.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
} // namespace

OpalineAudioProcessor::OpalineAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "OpalineFMParameters", createParameterLayout())
{
    state.patch = opaline::normalizePatch(state.patch);
    state.renderModel = renderModel;
    syncParametersFromState();
}

void OpalineAudioProcessor::prepareToPlay(const double sampleRate, int)
{
    const juce::ScopedLock lock(engineLock);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    engine.prepare(currentSampleRate, opaline::kDefaultMaxVoices);
    applyStateToEngine();
}

void OpalineAudioProcessor::releaseResources()
{
    const juce::ScopedLock lock(engineLock);
    engine.panic();
}

bool OpalineAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorValueTreeState::ParameterLayout OpalineAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    const auto intRange = [](const float min, const float max)
    {
        return juce::NormalisableRange<float>(min, max, 1.0f);
    };

    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::masterVolume, "Volume", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(param_ids::renderModel, "Engine", juce::StringArray { "OLD", "NEW" }, 1));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::algorithm, "Algorithm", intRange(1.0f, 8.0f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::feedback, "Feedback", intRange(0.0f, 7.0f), 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::transpose, "Transpose", intRange(-24.0f, 24.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(param_ids::lfoWave, "LFO Wave", juce::StringArray { "SAW UP", "SQUARE", "TRIANGLE", "S/H" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterBool>(param_ids::lfoSync, "LFO Sync", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::lfoSpeed, "LFO Speed", intRange(0.0f, 99.0f), 24.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::lfoDelay, "LFO Delay", intRange(0.0f, 99.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::lfoPitchDepth, "PMD", intRange(0.0f, 99.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::lfoAmpDepth, "AMD", intRange(0.0f, 99.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::lfoPitchSensitivity, "PMS", intRange(0.0f, 7.0f), 3.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::lfoAmpSensitivity, "AMS", intRange(0.0f, 3.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::pegRate1, "PR1", intRange(0.0f, 99.0f), 99.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::pegRate2, "PR2", intRange(0.0f, 99.0f), 99.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::pegRate3, "PR3", intRange(0.0f, 99.0f), 99.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::pegLevel1, "PL1", intRange(0.0f, 99.0f), 50.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::pegLevel2, "PL2", intRange(0.0f, 99.0f), 50.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::pegLevel3, "PL3", intRange(0.0f, 99.0f), 50.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::effectReverb, "Reverb", intRange(0.0f, 99.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::effectMix, "Mix", intRange(0.0f, 99.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::effectTone, "Tone", intRange(0.0f, 99.0f), 50.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::effectChorus, "Chorus", intRange(0.0f, 99.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::effectDelay, "Delay", intRange(0.0f, 99.0f), 0.0f));

    for (int op = 0; op < opaline::kOperatorCount; ++op)
    {
        params.push_back(std::make_unique<juce::AudioParameterBool>(opParamId(op, "Enabled"), opParamName(op, "Enabled"), true));
        params.push_back(std::make_unique<juce::AudioParameterBool>(opParamId(op, "AmpMod"), opParamName(op, "AM"), true));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opParamId(op, "Ratio"), opParamName(op, "Ratio"), intRange(0.0f, 63.0f), 4.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opParamId(op, "Detune"), opParamName(op, "Detune"), intRange(-3.0f, 3.0f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opParamId(op, "Level"), opParamName(op, "Level"), intRange(0.0f, 99.0f), 70.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opParamId(op, "RateScale"), opParamName(op, "Rate Scale"), intRange(0.0f, 3.0f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opParamId(op, "LevelScale"), opParamName(op, "Level Scale"), intRange(0.0f, 99.0f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opParamId(op, "Velocity"), opParamName(op, "Velocity"), intRange(0.0f, 7.0f), 2.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opParamId(op, "AR"), opParamName(op, "AR"), intRange(0.0f, 31.0f), 20.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opParamId(op, "D1R"), opParamName(op, "D1R"), intRange(0.0f, 31.0f), 8.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opParamId(op, "D1L"), opParamName(op, "D1L"), intRange(0.0f, 15.0f), 12.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opParamId(op, "D2R"), opParamName(op, "D2R"), intRange(0.0f, 31.0f), 1.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opParamId(op, "RR"), opParamName(op, "RR"), intRange(0.0f, 15.0f), 6.0f));
    }

    return { params.begin(), params.end() };
}

void OpalineAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const juce::ScopedLock lock(engineLock);
    applyParametersToState();

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

juce::AudioProcessorEditor* OpalineAudioProcessor::createEditor()
{
    return new OpalineAudioProcessorEditor(*this);
}

const juce::String OpalineAudioProcessor::getProgramName(int)
{
    const juce::ScopedLock lock(engineLock);
    return currentProgramName;
}

void OpalineAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const juce::ScopedLock lock(engineLock);
    const auto tree = opalineapp::synthStateToValueTree(state);
    if (auto xml = tree.createXml())
        copyXmlToBinary(*xml, destData);
}

void OpalineAudioProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        const auto tree = juce::ValueTree::fromXml(*xml);
        const juce::ScopedLock lock(engineLock);
        state = opalineapp::synthStateFromValueTree(tree, state);
        applyStateToEngine();
        syncParametersFromState();
    }
}

opalineapp::SynthState OpalineAudioProcessor::getSynthState() const
{
    const juce::ScopedLock lock(engineLock);
    return state;
}

void OpalineAudioProcessor::setSynthStateFromEditor(const opalineapp::SynthState& newState)
{
    const juce::ScopedLock lock(engineLock);
    state = newState;
    applyStateToEngine();
    syncParametersFromState();
}

void OpalineAudioProcessor::setRenderModelFromEditor(const opaline::OpalineRenderModel newRenderModel)
{
    const juce::ScopedLock lock(engineLock);
    renderModel = newRenderModel;
    state.renderModel = renderModel;
    engine.setRenderModel(renderModel);
}

void OpalineAudioProcessor::noteOnFromEditor(const int note, const int velocity)
{
    const juce::ScopedLock lock(engineLock);
    engine.noteOn(note, velocity);
}

void OpalineAudioProcessor::noteOffFromEditor(const int note)
{
    const juce::ScopedLock lock(engineLock);
    engine.noteOff(note);
}

void OpalineAudioProcessor::allNotesOffFromEditor()
{
    const juce::ScopedLock lock(engineLock);
    engine.panic();
    for (auto& velocity : midiUiVelocities)
        velocity.store(0, std::memory_order_relaxed);
}

void OpalineAudioProcessor::setPitchBendFromEditor(const double value)
{
    const juce::ScopedLock lock(engineLock);
    currentPitchBend = opaline::clampDouble(value, -1.0, 1.0);
    engine.setPitchBend(currentPitchBend);
}

void OpalineAudioProcessor::setModWheelFromEditor(const double value)
{
    const juce::ScopedLock lock(engineLock);
    currentModWheel = opaline::clampDouble(value, 0.0, 1.0);
    engine.setModWheel(currentModWheel);
}

void OpalineAudioProcessor::setProgramNameFromEditor(const juce::String& name)
{
    const juce::ScopedLock lock(engineLock);
    currentProgramName = name.isNotEmpty() ? name : "Opaline FM";
}

double OpalineAudioProcessor::getCurrentPitchBend() const
{
    const juce::ScopedLock lock(engineLock);
    return currentPitchBend;
}

double OpalineAudioProcessor::getCurrentModWheel() const
{
    const juce::ScopedLock lock(engineLock);
    return currentModWheel;
}

std::array<int, 128> OpalineAudioProcessor::getMidiUiVelocities() const
{
    std::array<int, 128> velocities {};
    for (std::size_t i = 0; i < velocities.size(); ++i)
        velocities[i] = midiUiVelocities[i].load(std::memory_order_relaxed);
    return velocities;
}

void OpalineAudioProcessor::handleMidiMessage(const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        const int note = message.getNoteNumber();
        const int velocity = juce::jlimit(1, 127, static_cast<int>(message.getVelocity()));
        if (juce::isPositiveAndBelow(note, static_cast<int>(midiUiVelocities.size())))
            midiUiVelocities[static_cast<std::size_t>(note)].store(velocity, std::memory_order_relaxed);
        engine.noteOn(note, velocity);
        return;
    }

    if (message.isNoteOff())
    {
        const int note = message.getNoteNumber();
        if (juce::isPositiveAndBelow(note, static_cast<int>(midiUiVelocities.size())))
            midiUiVelocities[static_cast<std::size_t>(note)].store(0, std::memory_order_relaxed);
        engine.noteOff(note);
        return;
    }

    if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        engine.panic();
        for (auto& velocity : midiUiVelocities)
            velocity.store(0, std::memory_order_relaxed);
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
        currentModWheel = opaline::clampDouble(static_cast<double>(message.getControllerValue()) / 127.0, 0.0, 1.0);
        engine.setModWheel(currentModWheel);
    }
}

void OpalineAudioProcessor::applyStateToEngine()
{
    state.patch = opaline::normalizePatch(state.patch);
    state.masterVolume = juce::jlimit(0.0f, 1.0f, state.masterVolume);
    renderModel = state.renderModel;
    engine.setRenderModel(renderModel);
    engine.setPatch(state.patch);
    engine.setPitchBend(currentPitchBend);
    engine.setModWheel(currentModWheel);
}

void OpalineAudioProcessor::applyParametersToState()
{
    state.masterVolume = juce::jlimit(0.0f, 1.0f, parameterFloat(parameters, param_ids::masterVolume, state.masterVolume));
    state.renderModel = parameterInt(parameters, param_ids::renderModel, static_cast<int>(state.renderModel)) == 0
        ? opaline::OpalineRenderModel::Current
        : opaline::OpalineRenderModel::ChipHybrid;

    auto patch = state.patch;
    patch.algorithm = parameterInt(parameters, param_ids::algorithm, patch.algorithm);
    patch.feedback = parameterInt(parameters, param_ids::feedback, patch.feedback);
    patch.transpose = parameterInt(parameters, param_ids::transpose, patch.transpose);
    patch.lfo.wave = parameterInt(parameters, param_ids::lfoWave, patch.lfo.wave);
    patch.lfo.sync = parameterBool(parameters, param_ids::lfoSync, patch.lfo.sync);
    patch.lfo.speed = parameterInt(parameters, param_ids::lfoSpeed, patch.lfo.speed);
    patch.lfo.delay = parameterInt(parameters, param_ids::lfoDelay, patch.lfo.delay);
    patch.lfo.pitchDepth = parameterInt(parameters, param_ids::lfoPitchDepth, patch.lfo.pitchDepth);
    patch.lfo.ampDepth = parameterInt(parameters, param_ids::lfoAmpDepth, patch.lfo.ampDepth);
    patch.lfo.pitchSensitivity = parameterInt(parameters, param_ids::lfoPitchSensitivity, patch.lfo.pitchSensitivity);
    patch.lfo.ampSensitivity = parameterInt(parameters, param_ids::lfoAmpSensitivity, patch.lfo.ampSensitivity);
    patch.pitchEnvelope.rate1 = parameterInt(parameters, param_ids::pegRate1, patch.pitchEnvelope.rate1);
    patch.pitchEnvelope.rate2 = parameterInt(parameters, param_ids::pegRate2, patch.pitchEnvelope.rate2);
    patch.pitchEnvelope.rate3 = parameterInt(parameters, param_ids::pegRate3, patch.pitchEnvelope.rate3);
    patch.pitchEnvelope.level1 = parameterInt(parameters, param_ids::pegLevel1, patch.pitchEnvelope.level1);
    patch.pitchEnvelope.level2 = parameterInt(parameters, param_ids::pegLevel2, patch.pitchEnvelope.level2);
    patch.pitchEnvelope.level3 = parameterInt(parameters, param_ids::pegLevel3, patch.pitchEnvelope.level3);
    patch.effects.reverb = parameterInt(parameters, param_ids::effectReverb, patch.effects.reverb);
    patch.effects.mix = parameterInt(parameters, param_ids::effectMix, patch.effects.mix);
    patch.effects.tone = parameterInt(parameters, param_ids::effectTone, patch.effects.tone);
    patch.effects.chorus = parameterInt(parameters, param_ids::effectChorus, patch.effects.chorus);
    patch.effects.delay = parameterInt(parameters, param_ids::effectDelay, patch.effects.delay);

    for (int opIndex = 0; opIndex < opaline::kOperatorCount; ++opIndex)
    {
        auto& op = patch.operators[static_cast<std::size_t>(opIndex)];
        op.enabled = parameterBool(parameters, opParamId(opIndex, "Enabled"), op.enabled);
        op.ampModEnable = parameterBool(parameters, opParamId(opIndex, "AmpMod"), op.ampModEnable);
        op.ratioIndex = parameterInt(parameters, opParamId(opIndex, "Ratio"), op.ratioIndex);
        op.detune = parameterInt(parameters, opParamId(opIndex, "Detune"), op.detune);
        op.level = parameterInt(parameters, opParamId(opIndex, "Level"), op.level);
        op.rateScale = parameterInt(parameters, opParamId(opIndex, "RateScale"), op.rateScale);
        op.levelScale = parameterInt(parameters, opParamId(opIndex, "LevelScale"), op.levelScale);
        op.velocity = parameterInt(parameters, opParamId(opIndex, "Velocity"), op.velocity);
        op.envelope.attackRate = parameterInt(parameters, opParamId(opIndex, "AR"), op.envelope.attackRate);
        op.envelope.decay1Rate = parameterInt(parameters, opParamId(opIndex, "D1R"), op.envelope.decay1Rate);
        op.envelope.decay1Level = parameterInt(parameters, opParamId(opIndex, "D1L"), op.envelope.decay1Level);
        op.envelope.decay2Rate = parameterInt(parameters, opParamId(opIndex, "D2R"), op.envelope.decay2Rate);
        op.envelope.releaseRate = parameterInt(parameters, opParamId(opIndex, "RR"), op.envelope.releaseRate);
    }

    state.patch = opaline::normalizePatch(patch);
    applyStateToEngine();
}

void OpalineAudioProcessor::syncParametersFromState()
{
    setApvtsParameter(parameters, param_ids::masterVolume, state.masterVolume);
    setApvtsParameter(parameters, param_ids::renderModel, state.renderModel == opaline::OpalineRenderModel::ChipHybrid ? 1.0f : 0.0f);
    setApvtsParameter(parameters, param_ids::algorithm, static_cast<float>(state.patch.algorithm));
    setApvtsParameter(parameters, param_ids::feedback, static_cast<float>(state.patch.feedback));
    setApvtsParameter(parameters, param_ids::transpose, static_cast<float>(state.patch.transpose));
    setApvtsParameter(parameters, param_ids::lfoWave, static_cast<float>(state.patch.lfo.wave));
    setApvtsParameter(parameters, param_ids::lfoSync, state.patch.lfo.sync ? 1.0f : 0.0f);
    setApvtsParameter(parameters, param_ids::lfoSpeed, static_cast<float>(state.patch.lfo.speed));
    setApvtsParameter(parameters, param_ids::lfoDelay, static_cast<float>(state.patch.lfo.delay));
    setApvtsParameter(parameters, param_ids::lfoPitchDepth, static_cast<float>(state.patch.lfo.pitchDepth));
    setApvtsParameter(parameters, param_ids::lfoAmpDepth, static_cast<float>(state.patch.lfo.ampDepth));
    setApvtsParameter(parameters, param_ids::lfoPitchSensitivity, static_cast<float>(state.patch.lfo.pitchSensitivity));
    setApvtsParameter(parameters, param_ids::lfoAmpSensitivity, static_cast<float>(state.patch.lfo.ampSensitivity));
    setApvtsParameter(parameters, param_ids::pegRate1, static_cast<float>(state.patch.pitchEnvelope.rate1));
    setApvtsParameter(parameters, param_ids::pegRate2, static_cast<float>(state.patch.pitchEnvelope.rate2));
    setApvtsParameter(parameters, param_ids::pegRate3, static_cast<float>(state.patch.pitchEnvelope.rate3));
    setApvtsParameter(parameters, param_ids::pegLevel1, static_cast<float>(state.patch.pitchEnvelope.level1));
    setApvtsParameter(parameters, param_ids::pegLevel2, static_cast<float>(state.patch.pitchEnvelope.level2));
    setApvtsParameter(parameters, param_ids::pegLevel3, static_cast<float>(state.patch.pitchEnvelope.level3));
    setApvtsParameter(parameters, param_ids::effectReverb, static_cast<float>(state.patch.effects.reverb));
    setApvtsParameter(parameters, param_ids::effectMix, static_cast<float>(state.patch.effects.mix));
    setApvtsParameter(parameters, param_ids::effectTone, static_cast<float>(state.patch.effects.tone));
    setApvtsParameter(parameters, param_ids::effectChorus, static_cast<float>(state.patch.effects.chorus));
    setApvtsParameter(parameters, param_ids::effectDelay, static_cast<float>(state.patch.effects.delay));

    for (int opIndex = 0; opIndex < opaline::kOperatorCount; ++opIndex)
    {
        const auto& op = state.patch.operators[static_cast<std::size_t>(opIndex)];
        setApvtsParameter(parameters, opParamId(opIndex, "Enabled"), op.enabled ? 1.0f : 0.0f);
        setApvtsParameter(parameters, opParamId(opIndex, "AmpMod"), op.ampModEnable ? 1.0f : 0.0f);
        setApvtsParameter(parameters, opParamId(opIndex, "Ratio"), static_cast<float>(op.ratioIndex));
        setApvtsParameter(parameters, opParamId(opIndex, "Detune"), static_cast<float>(op.detune));
        setApvtsParameter(parameters, opParamId(opIndex, "Level"), static_cast<float>(op.level));
        setApvtsParameter(parameters, opParamId(opIndex, "RateScale"), static_cast<float>(op.rateScale));
        setApvtsParameter(parameters, opParamId(opIndex, "LevelScale"), static_cast<float>(op.levelScale));
        setApvtsParameter(parameters, opParamId(opIndex, "Velocity"), static_cast<float>(op.velocity));
        setApvtsParameter(parameters, opParamId(opIndex, "AR"), static_cast<float>(op.envelope.attackRate));
        setApvtsParameter(parameters, opParamId(opIndex, "D1R"), static_cast<float>(op.envelope.decay1Rate));
        setApvtsParameter(parameters, opParamId(opIndex, "D1L"), static_cast<float>(op.envelope.decay1Level));
        setApvtsParameter(parameters, opParamId(opIndex, "D2R"), static_cast<float>(op.envelope.decay2Rate));
        setApvtsParameter(parameters, opParamId(opIndex, "RR"), static_cast<float>(op.envelope.releaseRate));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OpalineAudioProcessor();
}
