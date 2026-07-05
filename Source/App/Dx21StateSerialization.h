#pragma once

#include "App/Dx21AppState.h"

#include <juce_data_structures/juce_data_structures.h>

namespace dx21app
{
namespace state_ids
{
static const juce::Identifier synthState { "DX21SynthState" };
static const juce::Identifier patch { "Patch" };
static const juce::Identifier performance { "Performance" };
static const juce::Identifier lfo { "Lfo" };
static const juce::Identifier pitchEnvelope { "PitchEnvelope" };
static const juce::Identifier effects { "Effects" };
static const juce::Identifier op { "Operator" };
static const juce::Identifier envelope { "Envelope" };
} // namespace state_ids

inline int readInt(const juce::ValueTree& tree, const juce::Identifier& property, const int fallback)
{
    return static_cast<int>(tree.getProperty(property, fallback));
}

inline float readFloat(const juce::ValueTree& tree, const juce::Identifier& property, const float fallback)
{
    return static_cast<float>(tree.getProperty(property, fallback));
}

inline bool readBool(const juce::ValueTree& tree, const juce::Identifier& property, const bool fallback)
{
    return static_cast<bool>(tree.getProperty(property, fallback));
}

inline juce::ValueTree envelopeToValueTree(const dx21::Dx21EnvelopeParams& envelope)
{
    juce::ValueTree tree { state_ids::envelope };
    tree.setProperty("attackRate", envelope.attackRate, nullptr);
    tree.setProperty("decay1Rate", envelope.decay1Rate, nullptr);
    tree.setProperty("decay1Level", envelope.decay1Level, nullptr);
    tree.setProperty("decay2Rate", envelope.decay2Rate, nullptr);
    tree.setProperty("releaseRate", envelope.releaseRate, nullptr);
    return tree;
}

inline dx21::Dx21EnvelopeParams envelopeFromValueTree(const juce::ValueTree& tree,
                                                      const dx21::Dx21EnvelopeParams& fallback)
{
    dx21::Dx21EnvelopeParams envelope = fallback;
    if (!tree.isValid())
        return envelope;

    envelope.attackRate = readInt(tree, "attackRate", envelope.attackRate);
    envelope.decay1Rate = readInt(tree, "decay1Rate", envelope.decay1Rate);
    envelope.decay1Level = readInt(tree, "decay1Level", envelope.decay1Level);
    envelope.decay2Rate = readInt(tree, "decay2Rate", envelope.decay2Rate);
    envelope.releaseRate = readInt(tree, "releaseRate", envelope.releaseRate);
    return envelope;
}

inline juce::ValueTree operatorToValueTree(const dx21::Dx21Operator& op, const int index)
{
    juce::ValueTree tree { state_ids::op };
    tree.setProperty("index", index, nullptr);
    tree.setProperty("ratioIndex", op.ratioIndex, nullptr);
    tree.setProperty("detune", op.detune, nullptr);
    tree.setProperty("level", op.level, nullptr);
    tree.setProperty("rateScale", op.rateScale, nullptr);
    tree.setProperty("levelScale", op.levelScale, nullptr);
    tree.setProperty("velocity", op.velocity, nullptr);
    tree.setProperty("ampModEnable", op.ampModEnable, nullptr);
    tree.setProperty("enabled", op.enabled, nullptr);
    tree.addChild(envelopeToValueTree(op.envelope), -1, nullptr);
    return tree;
}

inline dx21::Dx21Operator operatorFromValueTree(const juce::ValueTree& tree,
                                                const dx21::Dx21Operator& fallback)
{
    dx21::Dx21Operator op = fallback;
    if (!tree.isValid())
        return op;

    op.ratioIndex = readInt(tree, "ratioIndex", op.ratioIndex);
    op.detune = readInt(tree, "detune", op.detune);
    op.level = readInt(tree, "level", op.level);
    op.rateScale = readInt(tree, "rateScale", op.rateScale);
    op.levelScale = readInt(tree, "levelScale", op.levelScale);
    op.velocity = readInt(tree, "velocity", op.velocity);
    op.ampModEnable = readBool(tree, "ampModEnable", op.ampModEnable);
    op.enabled = readBool(tree, "enabled", op.enabled);
    op.envelope = envelopeFromValueTree(tree.getChildWithName(state_ids::envelope), op.envelope);
    return op;
}

inline juce::ValueTree patchToValueTree(const dx21::Dx21Patch& patch)
{
    juce::ValueTree tree { state_ids::patch };
    tree.setProperty("algorithm", patch.algorithm, nullptr);
    tree.setProperty("feedback", patch.feedback, nullptr);
    tree.setProperty("transpose", patch.transpose, nullptr);

    juce::ValueTree lfo { state_ids::lfo };
    lfo.setProperty("speed", patch.lfo.speed, nullptr);
    lfo.setProperty("delay", patch.lfo.delay, nullptr);
    lfo.setProperty("pitchDepth", patch.lfo.pitchDepth, nullptr);
    lfo.setProperty("ampDepth", patch.lfo.ampDepth, nullptr);
    lfo.setProperty("pitchSensitivity", patch.lfo.pitchSensitivity, nullptr);
    lfo.setProperty("ampSensitivity", patch.lfo.ampSensitivity, nullptr);
    lfo.setProperty("sync", patch.lfo.sync, nullptr);
    lfo.setProperty("wave", patch.lfo.wave, nullptr);
    tree.addChild(lfo, -1, nullptr);

    juce::ValueTree pitchEnvelope { state_ids::pitchEnvelope };
    pitchEnvelope.setProperty("rate1", patch.pitchEnvelope.rate1, nullptr);
    pitchEnvelope.setProperty("rate2", patch.pitchEnvelope.rate2, nullptr);
    pitchEnvelope.setProperty("rate3", patch.pitchEnvelope.rate3, nullptr);
    pitchEnvelope.setProperty("level1", patch.pitchEnvelope.level1, nullptr);
    pitchEnvelope.setProperty("level2", patch.pitchEnvelope.level2, nullptr);
    pitchEnvelope.setProperty("level3", patch.pitchEnvelope.level3, nullptr);
    tree.addChild(pitchEnvelope, -1, nullptr);

    juce::ValueTree effects { state_ids::effects };
    effects.setProperty("reverb", patch.effects.reverb, nullptr);
    effects.setProperty("mix", patch.effects.mix, nullptr);
    effects.setProperty("tone", patch.effects.tone, nullptr);
    effects.setProperty("chorus", patch.effects.chorus, nullptr);
    effects.setProperty("delay", patch.effects.delay, nullptr);
    tree.addChild(effects, -1, nullptr);

    for (int i = 0; i < dx21::kOperatorCount; ++i)
        tree.addChild(operatorToValueTree(patch.operators[static_cast<std::size_t>(i)], i), -1, nullptr);

    return tree;
}

inline dx21::Dx21Patch patchFromValueTree(const juce::ValueTree& tree, const dx21::Dx21Patch& fallback)
{
    dx21::Dx21Patch patch = fallback;
    if (!tree.isValid())
        return dx21::normalizePatch(patch);

    patch.algorithm = readInt(tree, "algorithm", patch.algorithm);
    patch.feedback = readInt(tree, "feedback", patch.feedback);
    patch.transpose = readInt(tree, "transpose", patch.transpose);

    const auto lfo = tree.getChildWithName(state_ids::lfo);
    if (lfo.isValid())
    {
        patch.lfo.speed = readInt(lfo, "speed", patch.lfo.speed);
        patch.lfo.delay = readInt(lfo, "delay", patch.lfo.delay);
        patch.lfo.pitchDepth = readInt(lfo, "pitchDepth", patch.lfo.pitchDepth);
        patch.lfo.ampDepth = readInt(lfo, "ampDepth", patch.lfo.ampDepth);
        patch.lfo.pitchSensitivity = readInt(lfo, "pitchSensitivity", patch.lfo.pitchSensitivity);
        patch.lfo.ampSensitivity = readInt(lfo, "ampSensitivity", patch.lfo.ampSensitivity);
        patch.lfo.sync = readBool(lfo, "sync", patch.lfo.sync);
        patch.lfo.wave = readInt(lfo, "wave", patch.lfo.wave);
    }

    const auto pitchEnvelope = tree.getChildWithName(state_ids::pitchEnvelope);
    if (pitchEnvelope.isValid())
    {
        patch.pitchEnvelope.rate1 = readInt(pitchEnvelope, "rate1", patch.pitchEnvelope.rate1);
        patch.pitchEnvelope.rate2 = readInt(pitchEnvelope, "rate2", patch.pitchEnvelope.rate2);
        patch.pitchEnvelope.rate3 = readInt(pitchEnvelope, "rate3", patch.pitchEnvelope.rate3);
        patch.pitchEnvelope.level1 = readInt(pitchEnvelope, "level1", patch.pitchEnvelope.level1);
        patch.pitchEnvelope.level2 = readInt(pitchEnvelope, "level2", patch.pitchEnvelope.level2);
        patch.pitchEnvelope.level3 = readInt(pitchEnvelope, "level3", patch.pitchEnvelope.level3);
    }

    const auto effects = tree.getChildWithName(state_ids::effects);
    if (effects.isValid())
    {
        patch.effects.reverb = readInt(effects, "reverb", patch.effects.reverb);
        patch.effects.mix = readInt(effects, "mix", patch.effects.mix);
        patch.effects.tone = readInt(effects, "tone", patch.effects.tone);
        patch.effects.chorus = readInt(effects, "chorus", patch.effects.chorus);
        patch.effects.delay = readInt(effects, "delay", patch.effects.delay);
    }

    for (int childIndex = 0; childIndex < tree.getNumChildren(); ++childIndex)
    {
        const auto child = tree.getChild(childIndex);
        if (!child.hasType(state_ids::op))
            continue;

        const int opIndex = readInt(child, "index", -1);
        if (opIndex >= 0 && opIndex < dx21::kOperatorCount)
        {
            const auto size = static_cast<std::size_t>(opIndex);
            patch.operators[size] = operatorFromValueTree(child, patch.operators[size]);
        }
    }

    return dx21::normalizePatch(patch);
}

inline juce::ValueTree synthStateToValueTree(const SynthState& state)
{
    juce::ValueTree tree { state_ids::synthState };
    tree.setProperty("version", 1, nullptr);
    tree.setProperty("masterVolume", state.masterVolume, nullptr);
    tree.addChild(patchToValueTree(state.patch), -1, nullptr);

    juce::ValueTree performance { state_ids::performance };
    performance.setProperty("mode", static_cast<int>(state.performance.mode), nullptr);
    performance.setProperty("voiceAIndex", state.performance.voiceAIndex, nullptr);
    performance.setProperty("voiceBIndex", state.performance.voiceBIndex, nullptr);
    performance.setProperty("dualDetune", state.performance.dualDetune, nullptr);
    performance.setProperty("splitPoint", state.performance.splitPoint, nullptr);
    tree.addChild(performance, -1, nullptr);
    return tree;
}

inline SynthState synthStateFromValueTree(const juce::ValueTree& tree, const SynthState& fallback = {})
{
    SynthState state = fallback;
    if (!tree.hasType(state_ids::synthState))
        return state;

    state.masterVolume = juce::jlimit(0.0f, 1.0f, readFloat(tree, "masterVolume", state.masterVolume));
    state.patch = patchFromValueTree(tree.getChildWithName(state_ids::patch), state.patch);

    const auto performance = tree.getChildWithName(state_ids::performance);
    if (performance.isValid())
    {
        state.performance.mode = static_cast<PerformanceMode>(juce::jlimit(0, 2, readInt(performance, "mode", 0)));
        state.performance.voiceAIndex = readInt(performance, "voiceAIndex", state.performance.voiceAIndex);
        state.performance.voiceBIndex = readInt(performance, "voiceBIndex", state.performance.voiceBIndex);
        state.performance.dualDetune = readInt(performance, "dualDetune", state.performance.dualDetune);
        state.performance.splitPoint = juce::jlimit(0, 127, readInt(performance, "splitPoint", state.performance.splitPoint));
    }

    return state;
}
} // namespace dx21app
