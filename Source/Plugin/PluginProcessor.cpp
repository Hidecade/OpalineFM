#include "PluginProcessor.h"

#include "App/OpalineStateSerialization.h"
#include "App/OpalineVoiceLibraryXml.h"
#include "OpalineBinaryData.h"
#include "PluginEditor.h"

#include <juce_core/juce_core.h>

namespace
{
namespace param_ids
{
constexpr const char* masterVolume = "masterVolume";
constexpr const char* pitchBendRange = "pitchBendRange";
constexpr const char* portamento = "portamento";
constexpr const char* modWheelPitchRange = "modWheelPitchRange";
constexpr const char* modWheelAmpRange = "modWheelAmpRange";
constexpr const char* effectsEnabled = "effectsEnabled";
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
constexpr const char* effectEchoMix = "effectEchoMix";
constexpr const char* effectTone = "effectTone";
constexpr const char* effectChorus = "effectChorus";
constexpr const char* effectDelay = "effectDelay";
} // namespace param_ids

double pitchWheelToUnitBend(const int pitchWheelValue)
{
    return opaline::clampDouble((static_cast<double>(pitchWheelValue) - 8192.0) / 8192.0, -1.0, 1.0);
}

int parameterInt(const std::atomic<float>* const value, const int fallback) noexcept
{
    if (value != nullptr)
        return static_cast<int>(std::round(value->load()));

    return fallback;
}

float parameterFloat(const std::atomic<float>* const value, const float fallback) noexcept
{
    if (value != nullptr)
        return value->load();

    return fallback;
}

bool parameterBool(const std::atomic<float>* const value, const bool fallback) noexcept
{
    return parameterInt(value, fallback ? 1 : 0) != 0;
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
    loadFactoryPrograms();
    if (!factoryPrograms.empty())
    {
        state.performance.voiceAIndex = 0;
        state.patch = factoryPrograms.front().patch;
        state.effectsEnabled = factoryPrograms.front().effectsEnabled;
        currentProgramName = factoryProgramName(0);
    }

    state.patch = opaline::normalizePatch(state.patch);
    state.renderModel = opaline::OpalineRenderModel::TypeB;
    audioState = state;
    cacheParameterPointers();
    syncParametersFromState();

    for (auto* parameter : getParameters())
    {
        if (const auto* parameterWithID = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            parameters.addParameterListener(parameterWithID->paramID, this);
    }
}

OpalineAudioProcessor::~OpalineAudioProcessor()
{
    wavRecorder.stop();
    for (auto* parameter : getParameters())
    {
        if (const auto* parameterWithID = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            parameters.removeParameterListener(parameterWithID->paramID, this);
    }
}

void OpalineAudioProcessor::parameterChanged(const juce::String&, float)
{
    parametersDirty.store(true, std::memory_order_release);
}

void OpalineAudioProcessor::cacheParameterPointers()
{
    const auto get = [this](const juce::StringRef id)
    {
        return parameters.getRawParameterValue(id);
    };

    parameterPointers.masterVolume = get(param_ids::masterVolume);
    parameterPointers.pitchBendRange = get(param_ids::pitchBendRange);
    parameterPointers.portamento = get(param_ids::portamento);
    parameterPointers.modWheelPitchRange = get(param_ids::modWheelPitchRange);
    parameterPointers.modWheelAmpRange = get(param_ids::modWheelAmpRange);
    parameterPointers.effectsEnabled = get(param_ids::effectsEnabled);
    parameterPointers.algorithm = get(param_ids::algorithm);
    parameterPointers.feedback = get(param_ids::feedback);
    parameterPointers.transpose = get(param_ids::transpose);
    parameterPointers.lfoWave = get(param_ids::lfoWave);
    parameterPointers.lfoSync = get(param_ids::lfoSync);
    parameterPointers.lfoSpeed = get(param_ids::lfoSpeed);
    parameterPointers.lfoDelay = get(param_ids::lfoDelay);
    parameterPointers.lfoPitchDepth = get(param_ids::lfoPitchDepth);
    parameterPointers.lfoAmpDepth = get(param_ids::lfoAmpDepth);
    parameterPointers.lfoPitchSensitivity = get(param_ids::lfoPitchSensitivity);
    parameterPointers.lfoAmpSensitivity = get(param_ids::lfoAmpSensitivity);
    parameterPointers.pegRate1 = get(param_ids::pegRate1);
    parameterPointers.pegRate2 = get(param_ids::pegRate2);
    parameterPointers.pegRate3 = get(param_ids::pegRate3);
    parameterPointers.pegLevel1 = get(param_ids::pegLevel1);
    parameterPointers.pegLevel2 = get(param_ids::pegLevel2);
    parameterPointers.pegLevel3 = get(param_ids::pegLevel3);
    parameterPointers.effectReverb = get(param_ids::effectReverb);
    parameterPointers.effectMix = get(param_ids::effectMix);
    parameterPointers.effectEchoMix = get(param_ids::effectEchoMix);
    parameterPointers.effectTone = get(param_ids::effectTone);
    parameterPointers.effectChorus = get(param_ids::effectChorus);
    parameterPointers.effectDelay = get(param_ids::effectDelay);

    for (int opIndex = 0; opIndex < opaline::kOperatorCount; ++opIndex)
    {
        auto& op = parameterPointers.operators[static_cast<std::size_t>(opIndex)];
        op.enabled = get(opParamId(opIndex, "Enabled"));
        op.ampMod = get(opParamId(opIndex, "AmpMod"));
        op.ratio = get(opParamId(opIndex, "Ratio"));
        op.detune = get(opParamId(opIndex, "Detune"));
        op.level = get(opParamId(opIndex, "Level"));
        op.rateScale = get(opParamId(opIndex, "RateScale"));
        op.levelScale = get(opParamId(opIndex, "LevelScale"));
        op.velocity = get(opParamId(opIndex, "Velocity"));
        op.attackRate = get(opParamId(opIndex, "AR"));
        op.decay1Rate = get(opParamId(opIndex, "D1R"));
        op.decay1Level = get(opParamId(opIndex, "D1L"));
        op.decay2Rate = get(opParamId(opIndex, "D2R"));
        op.releaseRate = get(opParamId(opIndex, "RR"));
    }
}

void OpalineAudioProcessor::prepareToPlay(const double sampleRate, int)
{
    const double safeSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    currentSampleRate.store(safeSampleRate, std::memory_order_release);
    audioState = getSynthState();
    preparedPerformanceMode = audioState.performance.mode;
    engine.prepare(safeSampleRate, 8);
    performanceEngineB.prepare(safeSampleRate, 4);
    opalineapp::SynthState discardedState;
    while (stateUpdates.consume(discardedState)) {}
    parametersDirty.store(false, std::memory_order_release);
    applyAudioStateToEngine();
}

void OpalineAudioProcessor::releaseResources()
{
    panicEngines();
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

    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::masterVolume, "Volume", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.65f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::pitchBendRange, "Pitch Bend Range", intRange(0.0f, 12.0f), 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::portamento, "Portamento", intRange(0.0f, 99.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::modWheelPitchRange, "Mod Wheel Pitch", intRange(0.0f, 99.0f), 99.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::modWheelAmpRange, "Mod Wheel Amplitude", intRange(0.0f, 99.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(param_ids::effectsEnabled, "Effects", true));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(param_ids::renderModel, "Engine", juce::StringArray { "TYPE B", "TYPE B" }, 1));
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
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::effectMix, "Reverb Mix", intRange(0.0f, 99.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(param_ids::effectEchoMix, "Delay Mix", intRange(0.0f, 99.0f), 0.0f));
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
    opalineapp::SynthState updatedState;
    if (stateUpdates.consume(updatedState))
    {
        audioState = updatedState;
        applyAudioStateToEngine();
    }
    if (parametersDirty.exchange(false, std::memory_order_acq_rel))
        applyParametersToAudioState();
    applyRealtimeCommands();

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    for (int channel = 2; channel < numChannels; ++channel)
        buffer.clear(channel, 0, numSamples);

    auto* leftOutput = numChannels > 0 ? buffer.getWritePointer(0) : nullptr;
    auto* rightOutput = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;
    const bool renderPerformanceB = audioState.performance.mode != opalineapp::PerformanceMode::Single;
    const float gain = audioState.masterVolume;
    const float balance = static_cast<float>(juce::jlimit(-100, 100, audioState.performance.abBalance)) / 100.0f;
    const float gainA = balance >= 0.0f ? 1.0f : 1.0f + balance;
    const float gainB = balance <= 0.0f ? 1.0f : 1.0f - balance;
    const float mixGain = audioState.performance.mode == opalineapp::PerformanceMode::Dual ? 0.50f : 0.82f;

    auto midi = midiMessages.cbegin();
    const auto midiEnd = midiMessages.cend();
    for (int sample = 0; sample < numSamples; ++sample)
    {
        while (midi != midiEnd && (*midi).samplePosition <= sample)
        {
            handleMidiMessage(*midi);
            ++midi;
        }

        const auto outputA = engine.renderSample();
        auto output = outputA;
        if (renderPerformanceB)
        {
            const auto outputB = performanceEngineB.renderSample();
            output.left = (outputA.left * gainA + outputB.left * gainB) * mixGain;
            output.right = (outputA.right * gainA + outputB.right * gainB) * mixGain;
        }
        const float left = output.left * gain;
        const float right = output.right * gain;
        if (leftOutput != nullptr)
            leftOutput[sample] = left;
        if (rightOutput != nullptr)
            rightOutput[sample] = right;
    }

    if (leftOutput != nullptr)
    {
        const auto* right = rightOutput != nullptr ? rightOutput : leftOutput;
        scopeBuffer.push(leftOutput, numSamples);
        wavRecorder.push(leftOutput, right, numSamples);
    }

    midiMessages.clear();
}

juce::AudioProcessorEditor* OpalineAudioProcessor::createEditor()
{
    return new OpalineAudioProcessorEditor(*this);
}

int OpalineAudioProcessor::getNumPrograms()
{
    return juce::jmax(1, static_cast<int>(factoryPrograms.size()));
}

int OpalineAudioProcessor::getCurrentProgram()
{
    const juce::ScopedLock lock(stateLock);
    return juce::jlimit(0, getNumPrograms() - 1, state.performance.voiceAIndex);
}

void OpalineAudioProcessor::setCurrentProgram(const int index)
{
    const juce::ScopedLock lock(stateLock);
    if (factoryPrograms.empty())
        return;

    const int safeIndex = juce::jlimit(0, static_cast<int>(factoryPrograms.size()) - 1, index);
    state.performance.voiceAIndex = safeIndex;
    const auto& selectedProgram = factoryPrograms[static_cast<std::size_t>(safeIndex)];
    state.patch = opaline::normalizePatch(selectedProgram.patch);
    state.effectsEnabled = selectedProgram.effectsEnabled;
    currentProgramName = factoryProgramName(safeIndex);
    state.patch = opaline::normalizePatch(state.patch);
    publishStateUpdate();
    syncParametersFromState();
}

const juce::String OpalineAudioProcessor::getProgramName(const int index)
{
    const juce::ScopedLock lock(stateLock);
    if (index < 0)
        return currentProgramName;

    return factoryProgramName(index);
}

void OpalineAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const auto tree = opalineapp::synthStateToValueTree(getSynthState());
    if (auto xml = tree.createXml())
        copyXmlToBinary(*xml, destData);
}

void OpalineAudioProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        const auto tree = juce::ValueTree::fromXml(*xml);
        const juce::ScopedLock lock(stateLock);
        state = opalineapp::synthStateFromValueTree(tree, state);
        if (!factoryPrograms.empty())
        {
            const int maxVoiceIndex = static_cast<int>(factoryPrograms.size()) - 1;
            state.performance.voiceAIndex = juce::jlimit(0, maxVoiceIndex, state.performance.voiceAIndex);
            state.performance.voiceBIndex = juce::jlimit(0, maxVoiceIndex, state.performance.voiceBIndex);
            state.patch = patchForVoiceIndex(state.performance.voiceAIndex, state);
        }
        state.patch = opaline::normalizePatch(state.patch);
        state.renderModel = opaline::OpalineRenderModel::TypeB;
        currentProgramName = factoryProgramName(state.performance.voiceAIndex);
        publishStateUpdate();
        syncParametersFromState();
    }
}

opalineapp::SynthState OpalineAudioProcessor::getSynthState() const
{
    opalineapp::SynthState snapshot;
    {
        const juce::ScopedLock lock(stateLock);
        snapshot = state;
    }
    applyParametersToState(snapshot);
    snapshot.patch = opaline::normalizePatch(snapshot.patch);
    snapshot.renderModel = opaline::OpalineRenderModel::TypeB;
    return snapshot;
}

void OpalineAudioProcessor::setSynthStateFromEditor(const opalineapp::SynthState& newState)
{
    const juce::ScopedLock lock(stateLock);
    state = newState;
    state.patch = opaline::normalizePatch(state.patch);
    state.renderModel = opaline::OpalineRenderModel::TypeB;
    publishStateUpdate();
    syncParametersFromState();
}

void OpalineAudioProcessor::setRenderModelFromEditor(const opaline::OpalineRenderModel newRenderModel)
{
    (void) newRenderModel;
    const juce::ScopedLock lock(stateLock);
    state.renderModel = opaline::OpalineRenderModel::TypeB;
    publishStateUpdate();
}

void OpalineAudioProcessor::noteOnFromEditor(const int note, const int velocity)
{
    if (juce::isPositiveAndBelow(note, static_cast<int>(midiUiVelocities.size())))
        midiUiVelocities[static_cast<std::size_t>(note)].store(juce::jlimit(1, 127, velocity),
                                                               std::memory_order_relaxed);

    enqueueRealtimeCommand({ RealtimeCommandType::noteOn, note, velocity });
}

void OpalineAudioProcessor::noteOffFromEditor(const int note)
{
    if (juce::isPositiveAndBelow(note, static_cast<int>(midiUiVelocities.size())))
        midiUiVelocities[static_cast<std::size_t>(note)].store(0, std::memory_order_relaxed);

    enqueueRealtimeCommand({ RealtimeCommandType::noteOff, note });
}

void OpalineAudioProcessor::allNotesOffFromEditor()
{
    enqueueRealtimeCommand({ RealtimeCommandType::panic });
    for (auto& velocity : midiUiVelocities)
        velocity.store(0, std::memory_order_relaxed);
}

void OpalineAudioProcessor::setPitchBendFromEditor(const double value)
{
    const double safeValue = opaline::clampDouble(value, -1.0, 1.0);
    currentPitchBend.store(safeValue, std::memory_order_relaxed);
    enqueueRealtimeCommand({ RealtimeCommandType::pitchBend, 0, 0, safeValue });
}

void OpalineAudioProcessor::setModWheelFromEditor(const double value)
{
    const double safeValue = opaline::clampDouble(value, 0.0, 1.0);
    currentModWheel.store(safeValue, std::memory_order_relaxed);
    enqueueRealtimeCommand({ RealtimeCommandType::modWheel, 0, 0, safeValue });
}

void OpalineAudioProcessor::setProgramNameFromEditor(const juce::String& name)
{
    const juce::ScopedLock lock(stateLock);
    currentProgramName = name.isNotEmpty() ? name : "Opaline FM";
}

double OpalineAudioProcessor::getCurrentPitchBend() const
{
    return currentPitchBend.load(std::memory_order_relaxed);
}

double OpalineAudioProcessor::getCurrentModWheel() const
{
    return currentModWheel.load(std::memory_order_relaxed);
}

std::array<int, 128> OpalineAudioProcessor::getMidiUiVelocities() const
{
    std::array<int, 128> velocities {};
    for (std::size_t i = 0; i < velocities.size(); ++i)
        velocities[i] = midiUiVelocities[i].load(std::memory_order_relaxed);
    return velocities;
}

std::array<float, 4096> OpalineAudioProcessor::getScopeSamples() const
{
    std::array<float, 4096> samples {};
    scopeBuffer.drain(scopeHistory, scopeHistoryWriteIndex);
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        const auto index = (scopeHistoryWriteIndex + i) & (scopeHistory.size() - 1);
        samples[i] = scopeHistory[index];
    }

    return samples;
}

void OpalineAudioProcessor::startWavRecording()
{
    wavRecorder.start(currentSampleRate.load(std::memory_order_acquire));
}

void OpalineAudioProcessor::stopWavRecording()
{
    wavRecorder.stop();
}

bool OpalineAudioProcessor::stopWavRecordingAndSaveToFile(const juce::File& file)
{
    stopWavRecording();
    const double sampleRate = wavRecorder.sampleRate();
    auto interleaved = wavRecorder.takeRecordedSamples();

    const int sampleCount = static_cast<int>(interleaved.size() / 2);
    if (sampleCount <= 0)
        return false;

    juce::AudioBuffer<float> audio(2, sampleCount);
    auto* left = audio.getWritePointer(0);
    auto* right = audio.getWritePointer(1);
    for (int i = 0; i < sampleCount; ++i)
    {
        left[i] = interleaved[static_cast<std::size_t>(i) * 2];
        right[i] = interleaved[static_cast<std::size_t>(i) * 2 + 1];
    }

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::OutputStream> output(file.createOutputStream().release());
    if (output == nullptr)
        return false;

    auto writer = wavFormat.createWriterFor(output,
                                            juce::AudioFormatWriterOptions {}
                                                .withSampleRate(sampleRate)
                                                .withNumChannels(2)
                                                .withBitsPerSample(24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer(audio, 0, sampleCount);
}

void OpalineAudioProcessor::handleMidiMessage(const juce::MidiMessageMetadata& message) noexcept
{
    if (message.data == nullptr || message.numBytes <= 0)
        return;

    const auto status = static_cast<unsigned int>(message.data[0]);
    const auto type = status & 0xf0U;
    const auto data1 = message.numBytes > 1 ? static_cast<int>(message.data[1] & 0x7fU) : 0;
    const auto data2 = message.numBytes > 2 ? static_cast<int>(message.data[2] & 0x7fU) : 0;

    if (type == 0x90U && data2 > 0)
    {
        const int note = data1;
        const int velocity = data2;
        if (juce::isPositiveAndBelow(note, static_cast<int>(midiUiVelocities.size())))
            midiUiVelocities[static_cast<std::size_t>(note)].store(velocity, std::memory_order_relaxed);
        performNoteOn(note, velocity);
        return;
    }

    if (type == 0x80U || (type == 0x90U && data2 == 0))
    {
        const int note = data1;
        if (juce::isPositiveAndBelow(note, static_cast<int>(midiUiVelocities.size())))
            midiUiVelocities[static_cast<std::size_t>(note)].store(0, std::memory_order_relaxed);
        performNoteOff(note);
        return;
    }

    if (type == 0xb0U && (data1 == 120 || data1 == 123))
    {
        panicEngines();
        for (auto& velocity : midiUiVelocities)
            velocity.store(0, std::memory_order_relaxed);
        return;
    }

    if (type == 0xe0U && message.numBytes >= 3)
    {
        currentPitchBend.store(pitchWheelToUnitBend(data1 | (data2 << 7)), std::memory_order_relaxed);
        setPitchBendOnEngines();
        return;
    }

    if (type == 0xb0U && data1 == 1)
    {
        currentModWheel.store(opaline::clampDouble(static_cast<double>(data2) / 127.0, 0.0, 1.0),
                              std::memory_order_relaxed);
        setModWheelOnEngines();
        return;
    }

    if (type == 0xb0U && data1 == 64)
    {
        const bool pedalDown = data2 >= 64;
        engine.setSustainPedal(pedalDown);
        performanceEngineB.setSustainPedal(pedalDown);
        return;
    }

    if (type == 0xb0U && data1 == 65)
    {
        const bool pedalDown = data2 >= 64;
        engine.setPortamentoFootSwitch(pedalDown);
        performanceEngineB.setPortamentoFootSwitch(pedalDown);
    }
}

void OpalineAudioProcessor::applyAudioStateToEngine()
{
    audioState.patch = opaline::normalizePatch(audioState.patch);
    audioState.masterVolume = juce::jlimit(0.0f, 1.0f, audioState.masterVolume);
    audioState.pitchBendRange = juce::jlimit(0, 12, audioState.pitchBendRange);
    audioState.portamento = juce::jlimit(0, 99, audioState.portamento);
    audioState.modWheelPitchRange = juce::jlimit(0, 99, audioState.modWheelPitchRange);
    audioState.modWheelAmpRange = juce::jlimit(0, 99, audioState.modWheelAmpRange);
    audioState.renderModel = opaline::OpalineRenderModel::TypeB;
    if (preparedPerformanceMode != audioState.performance.mode)
    {
        preparedPerformanceMode = audioState.performance.mode;
        engine.setVoiceLimit(preparedPerformanceMode == opalineapp::PerformanceMode::Single ? 8 : 4);
        performanceEngineB.setVoiceLimit(4);
    }
    engine.setRenderModel(opaline::OpalineRenderModel::TypeB);
    performanceEngineB.setRenderModel(opaline::OpalineRenderModel::TypeB);
    engine.setPatch(audioState.patch);
    performanceEngineB.setPatch(patchForVoiceIndex(audioState.performance.voiceBIndex, audioState));
    engine.setPitchBendRange(audioState.pitchBendRange);
    performanceEngineB.setPitchBendRange(audioState.pitchBendRange);
    engine.setPortamento(audioState.portamento);
    performanceEngineB.setPortamento(audioState.portamento);
    engine.setModWheelRanges(audioState.modWheelPitchRange, audioState.modWheelAmpRange);
    performanceEngineB.setModWheelRanges(audioState.modWheelPitchRange, audioState.modWheelAmpRange);
    engine.setEffectsEnabled(audioState.effectsEnabled);
    performanceEngineB.setEffectsEnabled(audioState.effectsEnabled);
    engine.setMonoMode(audioState.performance.monoA);
    performanceEngineB.setMonoMode(audioState.performance.monoB);
    engine.setPortamentoMode(static_cast<int>(audioState.performance.portamentoModeA));
    performanceEngineB.setPortamentoMode(static_cast<int>(audioState.performance.portamentoModeB));
    setPitchBendOnEngines();
    setModWheelOnEngines();
}

opaline::OpalinePatch OpalineAudioProcessor::patchForVoiceIndex(
    const int index, const opalineapp::SynthState& targetState) const
{
    if (factoryPrograms.empty())
        return targetState.patch;

    const int safeIndex = juce::jlimit(0, static_cast<int>(factoryPrograms.size()) - 1, index);
    auto patch = factoryPrograms[static_cast<std::size_t>(safeIndex)].patch;
    patch.transpose = targetState.patch.transpose;
    return opaline::normalizePatch(patch);
}

void OpalineAudioProcessor::performNoteOn(const int note, const int velocity)
{
    switch (audioState.performance.mode)
    {
        case opalineapp::PerformanceMode::Single:
            engine.noteOn(note, velocity);
            break;

        case opalineapp::PerformanceMode::Dual:
            engine.noteOn(note, velocity);
            performanceEngineB.noteOn(note, velocity);
            break;

        case opalineapp::PerformanceMode::Split:
            if (note <= audioState.performance.splitPoint)
                engine.noteOn(note, velocity);
            else
                performanceEngineB.noteOn(note, velocity);
            break;
    }
}

void OpalineAudioProcessor::performNoteOff(const int note)
{
    engine.noteOff(note);
    performanceEngineB.noteOff(note);
}

void OpalineAudioProcessor::panicEngines()
{
    engine.panic();
    performanceEngineB.panic();
}

void OpalineAudioProcessor::setPitchBendOnEngines()
{
    const double pitchBend = currentPitchBend.load(std::memory_order_relaxed);
    engine.setPitchBend(pitchBend);
    performanceEngineB.setPitchBend(juce::jlimit(-1.0, 1.0,
        pitchBend + static_cast<double>(audioState.performance.dualDetune) / 64.0));
}

void OpalineAudioProcessor::setModWheelOnEngines()
{
    const double modWheel = currentModWheel.load(std::memory_order_relaxed);
    engine.setModWheel(modWheel);
    performanceEngineB.setModWheel(modWheel);
}

void OpalineAudioProcessor::applyParametersToState(opalineapp::SynthState& targetState) const noexcept
{
    targetState.masterVolume = juce::jlimit(0.0f, 1.0f,
        parameterFloat(parameterPointers.masterVolume, targetState.masterVolume));
    targetState.pitchBendRange = juce::jlimit(0, 12,
        parameterInt(parameterPointers.pitchBendRange, targetState.pitchBendRange));
    targetState.portamento = juce::jlimit(0, 99,
        parameterInt(parameterPointers.portamento, targetState.portamento));
    targetState.modWheelPitchRange = juce::jlimit(0, 99,
        parameterInt(parameterPointers.modWheelPitchRange, targetState.modWheelPitchRange));
    targetState.modWheelAmpRange = juce::jlimit(0, 99,
        parameterInt(parameterPointers.modWheelAmpRange, targetState.modWheelAmpRange));
    targetState.effectsEnabled = parameterBool(parameterPointers.effectsEnabled, targetState.effectsEnabled);
    targetState.renderModel = opaline::OpalineRenderModel::TypeB;

    auto patch = targetState.patch;
    patch.algorithm = parameterInt(parameterPointers.algorithm, patch.algorithm);
    patch.feedback = parameterInt(parameterPointers.feedback, patch.feedback);
    patch.transpose = parameterInt(parameterPointers.transpose, patch.transpose);
    patch.lfo.wave = parameterInt(parameterPointers.lfoWave, patch.lfo.wave);
    patch.lfo.sync = parameterBool(parameterPointers.lfoSync, patch.lfo.sync);
    patch.lfo.speed = parameterInt(parameterPointers.lfoSpeed, patch.lfo.speed);
    patch.lfo.delay = parameterInt(parameterPointers.lfoDelay, patch.lfo.delay);
    patch.lfo.pitchDepth = parameterInt(parameterPointers.lfoPitchDepth, patch.lfo.pitchDepth);
    patch.lfo.ampDepth = parameterInt(parameterPointers.lfoAmpDepth, patch.lfo.ampDepth);
    patch.lfo.pitchSensitivity = parameterInt(parameterPointers.lfoPitchSensitivity, patch.lfo.pitchSensitivity);
    patch.lfo.ampSensitivity = parameterInt(parameterPointers.lfoAmpSensitivity, patch.lfo.ampSensitivity);
    patch.pitchEnvelope.rate1 = parameterInt(parameterPointers.pegRate1, patch.pitchEnvelope.rate1);
    patch.pitchEnvelope.rate2 = parameterInt(parameterPointers.pegRate2, patch.pitchEnvelope.rate2);
    patch.pitchEnvelope.rate3 = parameterInt(parameterPointers.pegRate3, patch.pitchEnvelope.rate3);
    patch.pitchEnvelope.level1 = parameterInt(parameterPointers.pegLevel1, patch.pitchEnvelope.level1);
    patch.pitchEnvelope.level2 = parameterInt(parameterPointers.pegLevel2, patch.pitchEnvelope.level2);
    patch.pitchEnvelope.level3 = parameterInt(parameterPointers.pegLevel3, patch.pitchEnvelope.level3);
    patch.effects.reverb = parameterInt(parameterPointers.effectReverb, patch.effects.reverb);
    patch.effects.mix = parameterInt(parameterPointers.effectMix, patch.effects.mix);
    patch.effects.echoMix = parameterInt(parameterPointers.effectEchoMix, patch.effects.echoMix);
    patch.effects.tone = parameterInt(parameterPointers.effectTone, patch.effects.tone);
    patch.effects.chorus = parameterInt(parameterPointers.effectChorus, patch.effects.chorus);
    patch.effects.delay = parameterInt(parameterPointers.effectDelay, patch.effects.delay);

    for (int opIndex = 0; opIndex < opaline::kOperatorCount; ++opIndex)
    {
        const auto index = static_cast<std::size_t>(opIndex);
        auto& op = patch.operators[index];
        const auto& values = parameterPointers.operators[index];
        op.enabled = parameterBool(values.enabled, op.enabled);
        op.ampModEnable = parameterBool(values.ampMod, op.ampModEnable);
        op.ratioIndex = parameterInt(values.ratio, op.ratioIndex);
        op.detune = parameterInt(values.detune, op.detune);
        op.level = parameterInt(values.level, op.level);
        op.rateScale = parameterInt(values.rateScale, op.rateScale);
        op.levelScale = parameterInt(values.levelScale, op.levelScale);
        op.velocity = parameterInt(values.velocity, op.velocity);
        op.envelope.attackRate = parameterInt(values.attackRate, op.envelope.attackRate);
        op.envelope.decay1Rate = parameterInt(values.decay1Rate, op.envelope.decay1Rate);
        op.envelope.decay1Level = parameterInt(values.decay1Level, op.envelope.decay1Level);
        op.envelope.decay2Rate = parameterInt(values.decay2Rate, op.envelope.decay2Rate);
        op.envelope.releaseRate = parameterInt(values.releaseRate, op.envelope.releaseRate);
    }

    targetState.patch = opaline::normalizePatch(patch);
}

void OpalineAudioProcessor::applyParametersToAudioState()
{
    applyParametersToState(audioState);
    applyAudioStateToEngine();
}

void OpalineAudioProcessor::publishStateUpdate()
{
    stateUpdates.publish(state);
}

void OpalineAudioProcessor::enqueueRealtimeCommand(const RealtimeCommand& command) noexcept
{
    if (!realtimeCommands.push(command))
        realtimeCommandOverflowed.store(true, std::memory_order_release);
}

void OpalineAudioProcessor::applyRealtimeCommands()
{
    RealtimeCommand command;
    if (realtimeCommandOverflowed.exchange(false, std::memory_order_acq_rel))
    {
        while (realtimeCommands.pop(command)) {}
        panicEngines();
        return;
    }

    while (realtimeCommands.pop(command))
    {
        switch (command.type)
        {
            case RealtimeCommandType::noteOn:
                performNoteOn(command.value1, command.value2);
                break;
            case RealtimeCommandType::noteOff:
                performNoteOff(command.value1);
                break;
            case RealtimeCommandType::panic:
                panicEngines();
                break;
            case RealtimeCommandType::pitchBend:
                engine.setPitchBend(command.normalizedValue);
                performanceEngineB.setPitchBend(juce::jlimit(-1.0, 1.0,
                    command.normalizedValue
                        + static_cast<double>(audioState.performance.dualDetune) / 64.0));
                break;
            case RealtimeCommandType::modWheel:
                engine.setModWheel(command.normalizedValue);
                performanceEngineB.setModWheel(command.normalizedValue);
                break;
        }
    }

    if (realtimeCommandOverflowed.exchange(false, std::memory_order_acq_rel))
    {
        while (realtimeCommands.pop(command)) {}
        panicEngines();
    }
}

void OpalineAudioProcessor::syncParametersFromState()
{
    setApvtsParameter(parameters, param_ids::masterVolume, state.masterVolume);
    setApvtsParameter(parameters, param_ids::pitchBendRange, static_cast<float>(state.pitchBendRange));
    setApvtsParameter(parameters, param_ids::portamento, static_cast<float>(state.portamento));
    setApvtsParameter(parameters, param_ids::modWheelPitchRange, static_cast<float>(state.modWheelPitchRange));
    setApvtsParameter(parameters, param_ids::modWheelAmpRange, static_cast<float>(state.modWheelAmpRange));
    setApvtsParameter(parameters, param_ids::effectsEnabled, state.effectsEnabled ? 1.0f : 0.0f);
    setApvtsParameter(parameters, param_ids::renderModel, state.renderModel == opaline::OpalineRenderModel::TypeB ? 1.0f : 0.0f);
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
    setApvtsParameter(parameters, param_ids::effectEchoMix, static_cast<float>(state.patch.effects.echoMix));
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

void OpalineAudioProcessor::loadFactoryPrograms()
{
    factoryPrograms.clear();
    auto library = opaline::makeInitVoiceLibrary();
    const auto factoryXml = juce::parseXML(juce::String::fromUTF8(
        OpalineBinaryData::factory_opalinelibrary_xml,
        OpalineBinaryData::factory_opalinelibrary_xmlSize));
    const bool loadedFactoryLibrary = factoryXml != nullptr
        && opalineapp::voiceLibraryFromXml(*factoryXml, library);
    if (!loadedFactoryLibrary)
    {
        try
        {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(OpalineBinaryData::factory_syx);
            const std::vector<std::uint8_t> bytes(begin, begin + OpalineBinaryData::factory_syxSize);
            library.banks[0] = opaline::voiceBankFromSysex(bytes, "Factory");
        }
        catch (const std::exception&)
        {
            library.banks[0] = opaline::makeInitVoiceBank("Factory");
        }
    }

    factoryPrograms.reserve(opaline::kOpalineVoiceBankSize);
    for (const auto& voice : library.banks[0].voices)
        factoryPrograms.push_back(voice);
}

juce::String OpalineAudioProcessor::factoryProgramName(const int index) const
{
    if (factoryPrograms.empty())
        return "Opaline FM";

    const int safeIndex = juce::jlimit(0, static_cast<int>(factoryPrograms.size()) - 1, index);
    const auto& voice = factoryPrograms[static_cast<std::size_t>(safeIndex)];
    const auto bankPrefix = safeIndex < 16 ? "A" : "B";
    const auto number = juce::String(safeIndex % 16 + 1).paddedLeft('0', 2);
    auto name = juce::String(voice.name).trim();
    if (name.isEmpty())
        name = "INIT " + juce::String(safeIndex + 1);

    return juce::String(bankPrefix) + number + " " + name.substring(0, 12);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OpalineAudioProcessor();
}
