#include "Engine/OpalineEngine.h"

#include <algorithm>
#include <cmath>

namespace opaline
{
namespace
{
// 最終段の余裕を残すための全体ゲインと、簡易エフェクト用の最大ディレイ長。
constexpr double kOutputGain = 0.38;
constexpr double kLimiterThreshold = 0.86;
constexpr double kLimiterCeiling = 0.96;
constexpr double kMaxDelaySeconds = 0.8;
constexpr double kMaxChorusSeconds = 0.04;

double portamentoSecondsForValue(const int value)
{
    const double normalized = static_cast<double>(clampInt(value, 0, 99)) / 99.0;
    return 0.01 + 1.99 * normalized * normalized;
}

double softLimit(const double sample)
{
    const double magnitude = std::abs(sample);
    if (magnitude <= kLimiterThreshold)
        return sample;

    const double kneeRange = kLimiterCeiling - kLimiterThreshold;
    const double excess = magnitude - kLimiterThreshold;
    const double limited = kLimiterThreshold + kneeRange * (1.0 - std::exp(-excess / kneeRange));
    return std::copysign(std::min(limited, kLimiterCeiling), sample);
}

}

void OpalineEngine::prepare(const double sampleRate, const int maxVoices)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxVoiceCount = clampInt(maxVoices, 1, 32);

    // サンプルレート変更時はエフェクト用リングバッファを作り直す。
    delayBufferLeft.assign(static_cast<std::size_t>(std::ceil(currentSampleRate * kMaxDelaySeconds)) + 4, 0.0);
    delayBufferRight.assign(delayBufferLeft.size(), 0.0);
    chorusBufferLeft.assign(static_cast<std::size_t>(std::ceil(currentSampleRate * kMaxChorusSeconds)) + 4, 0.0);
    chorusBufferRight.assign(chorusBufferLeft.size(), 0.0);
    static constexpr std::array<double, 4> kReverbTimes { 0.031, 0.043, 0.057, 0.071 };
    for (int i = 0; i < 4; ++i)
    {
        const auto length = static_cast<std::size_t>(std::ceil(currentSampleRate * kReverbTimes[static_cast<std::size_t>(i)])) + 1;
        reverbBufferLeft[static_cast<std::size_t>(i)].assign(length, 0.0);
        reverbBufferRight[static_cast<std::size_t>(i)].assign(length, 0.0);
    }
    voices.clear();
    voices.reserve(static_cast<std::size_t>(maxVoiceCount));
    panic();
}

void OpalineEngine::setPatch(const OpalinePatch& newPatch)
{
    patch = normalizePatch(newPatch);
}

void OpalineEngine::noteOn(const int note, const int velocity)
{
    const int safeNote = clampInt(note, 0, 127);
    const bool hasHeldKey = std::any_of(keyDownNotes.begin(), keyDownNotes.end(), [](const bool down) { return down; });
    keyDownNotes[static_cast<std::size_t>(safeNote)] = true;
    sustainedNotes[static_cast<std::size_t>(safeNote)] = false;
    const bool fullPortamento = portamentoMode == 1 && portamentoFootSwitchDown;
    const bool fingeredPortamento = portamentoMode == 2 && hasHeldKey;

    if (monoMode && fingeredPortamento && !voices.empty())
    {
        voices.back().retargetPitch(safeNote, portamentoSecondsForValue(portamento));
        lastPlayedNote = safeNote;
        return;
    }

    if (monoMode)
        voices.clear();
    voices.erase(std::remove_if(voices.begin(),
                                voices.end(),
                                [safeNote](const OpalineVoice& voice) { return voice.note() == safeNote; }),
                 voices.end());

    OpalineVoice voice;
    const bool usePortamento = fullPortamento || fingeredPortamento;
    const int fromNote = usePortamento ? lastPlayedNote : -1;
    voice.start(patch, safeNote, clampInt(velocity, 0, 127), currentSampleRate, renderModel,
                fromNote, portamentoSecondsForValue(portamento));
    lastPlayedNote = safeNote;
    voices.push_back(voice);

    while (static_cast<int>(voices.size()) > maxVoiceCount)
        voices.erase(voices.begin());

    if (patch.lfo.sync)
        globalLfoAge = 0.0;
}

void OpalineEngine::noteOff(const int note)
{
    const int safeNote = clampInt(note, 0, 127);
    keyDownNotes[static_cast<std::size_t>(safeNote)] = false;
    if (sustainPedalDown)
    {
        sustainedNotes[static_cast<std::size_t>(safeNote)] = true;
        return;
    }

    for (auto& voice : voices)
    {
        if (voice.note() == safeNote)
            voice.release();
    }
}

void OpalineEngine::setPitchBend(const double value)
{
    pitchBend = clampDouble(value, -1.0, 1.0);
}

void OpalineEngine::setPitchBendRange(const int semitones)
{
    pitchBendRange = clampInt(semitones, 0, 12);
}

void OpalineEngine::setPortamento(const int value)
{
    portamento = clampInt(value, 0, 99);
}

void OpalineEngine::setPortamentoMode(const int mode)
{
    portamentoMode = clampInt(mode, 0, 2);
}

void OpalineEngine::setPortamentoFootSwitch(const bool down)
{
    portamentoFootSwitchDown = down;
}

void OpalineEngine::setEffectsEnabled(const bool enabled)
{
    if (effectsEnabled == enabled)
        return;

    effectsEnabled = enabled;
    if (!effectsEnabled)
        resetEffects();
}
void OpalineEngine::setMonoMode(const bool enabled)
{
    monoMode = enabled;
}

void OpalineEngine::setSustainPedal(const bool down)
{
    if (sustainPedalDown == down)
        return;

    sustainPedalDown = down;
    if (sustainPedalDown)
        return;

    for (int note = 0; note < static_cast<int>(sustainedNotes.size()); ++note)
    {
        if (!sustainedNotes[static_cast<std::size_t>(note)])
            continue;
        sustainedNotes[static_cast<std::size_t>(note)] = false;
        noteOff(note);
    }
}

void OpalineEngine::setModWheel(const double value)
{
    modWheel = clampDouble(value, 0.0, 1.0);
}

void OpalineEngine::setModWheelRanges(const int pitchRange, const int ampRange)
{
    modWheelPitchRange = clampInt(pitchRange, 0, 99);
    modWheelAmpRange = clampInt(ampRange, 0, 99);
}

void OpalineEngine::panic()
{
    voices.clear();
    sustainPedalDown = false;
    sustainedNotes.fill(false);
    keyDownNotes.fill(false);
    lastPlayedNote = -1;
    globalLfoAge = 0.0;
    lastOutput = 0.0;
    lastLeft = 0.0;
    lastRight = 0.0;
    resetEffects();
}

double OpalineEngine::limitAndDeclick(const double sample)
{
    const double limited = softLimit(sample);
    const double delta = clampDouble(limited - lastOutput, -0.42, 0.42);
    lastOutput += delta;
    return lastOutput;
}

void OpalineEngine::resetEffects()
{
    std::fill(delayBufferLeft.begin(), delayBufferLeft.end(), 0.0);
    std::fill(delayBufferRight.begin(), delayBufferRight.end(), 0.0);
    std::fill(chorusBufferLeft.begin(), chorusBufferLeft.end(), 0.0);
    std::fill(chorusBufferRight.begin(), chorusBufferRight.end(), 0.0);
    for (auto& buffer : reverbBufferLeft)
        std::fill(buffer.begin(), buffer.end(), 0.0);
    for (auto& buffer : reverbBufferRight)
        std::fill(buffer.begin(), buffer.end(), 0.0);
    reverbWriteIndices.fill(0);
    delayWriteIndex = 0;
    chorusWriteIndex = 0;
    chorusPhase = 0.0;
    toneLeft = 0.0;
    toneRight = 0.0;
}

double OpalineEngine::readDelay(const std::vector<double>& buffer, const int writeIndex, const double delaySamples) const
{
    if (buffer.empty())
        return 0.0;

    const auto size = static_cast<int>(buffer.size());
    double readPosition = static_cast<double>(writeIndex) - delaySamples;
    while (readPosition < 0.0)
        readPosition += static_cast<double>(size);
    while (readPosition >= static_cast<double>(size))
        readPosition -= static_cast<double>(size);

    const int i0 = static_cast<int>(std::floor(readPosition)) % size;
    const int i1 = (i0 + 1) % size;
    const double fraction = readPosition - std::floor(readPosition);
    return buffer[static_cast<std::size_t>(i0)] * (1.0 - fraction) + buffer[static_cast<std::size_t>(i1)] * fraction;
}

StereoSample OpalineEngine::processEffects(const double input)
{
    if (!effectsEnabled)
        return { static_cast<float>(input), static_cast<float>(input) };

    const auto& fx = patch.effects;
    const double reverb = static_cast<double>(fx.reverb) / 99.0;
    const double reverbMix = static_cast<double>(fx.mix) / 99.0;
    const double echoMix = static_cast<double>(fx.echoMix) / 99.0;
    const double tone = static_cast<double>(fx.tone) / 99.0;
    const double chorus = static_cast<double>(fx.chorus) / 99.0;
    const double delay = static_cast<double>(fx.delay) / 99.0;

    const double wetAmount = clampDouble(reverbMix + echoMix * 0.75 + chorus * 0.25, 0.0, 1.0);
    const double dryGain = 1.0 - wetAmount * 0.55;
    const double reverbWetGain = reverbMix * (0.18 + reverb * 0.82);
    const double echoWetGain = echoMix * (0.18 + delay * 0.82);

    const double reverbFeedback = 0.18 + reverb * 0.62;
    double reverbOutLeft = 0.0;
    double reverbOutRight = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        auto& leftBuffer = reverbBufferLeft[static_cast<std::size_t>(i)];
        auto& rightBuffer = reverbBufferRight[static_cast<std::size_t>(i)];
        if (leftBuffer.empty() || rightBuffer.empty())
            continue;

        int& index = reverbWriteIndices[static_cast<std::size_t>(i)];
        index %= static_cast<int>(leftBuffer.size());
        const auto leftIndex = static_cast<std::size_t>(index);
        const auto rightIndex = static_cast<std::size_t>(index);
        const double delayedLeft = leftBuffer[leftIndex];
        const double delayedRight = rightBuffer[rightIndex];
        const double sign = (i & 1) == 0 ? 1.0 : -1.0;
        reverbOutLeft += delayedLeft * sign;
        reverbOutRight += delayedRight * -sign;
        const double damping = 0.58 + reverb * 0.24;
        leftBuffer[leftIndex] = input + delayedRight * reverbFeedback * damping;
        rightBuffer[rightIndex] = input + delayedLeft * reverbFeedback * damping;
        index = (index + 1) % static_cast<int>(leftBuffer.size());
    }
    reverbOutLeft *= 0.25 * reverb;
    reverbOutRight *= 0.25 * reverb;

    const double delaySamples = delay * 0.52 * currentSampleRate;
    const double wetInLeft = input + reverbOutLeft * 0.35;
    const double wetInRight = input + reverbOutRight * 0.35;
    const double delayedLeft = delaySamples > 1.0 ? readDelay(delayBufferLeft, delayWriteIndex, delaySamples) : 0.0;
    const double delayedRight = delaySamples > 1.0 ? readDelay(delayBufferRight, delayWriteIndex, delaySamples) : 0.0;
    const double delayFeedback = delaySamples > 1.0 ? 0.10 + delay * 0.50 : 0.0;
    if (!delayBufferLeft.empty())
    {
        delayBufferLeft[static_cast<std::size_t>(delayWriteIndex)] = wetInLeft + toneRight * delayFeedback;
        delayBufferRight[static_cast<std::size_t>(delayWriteIndex)] = wetInRight + toneLeft * delayFeedback;
        delayWriteIndex = (delayWriteIndex + 1) % static_cast<int>(delayBufferLeft.size());
    }

    const double cutoff = 900.0 + tone * 11200.0;
    const double toneCoeff = 1.0 - std::exp(-2.0 * kPi * cutoff / currentSampleRate);
    toneLeft += (delayedLeft - toneLeft) * clampDouble(toneCoeff, 0.0, 1.0);
    toneRight += (delayedRight - toneRight) * clampDouble(toneCoeff, 0.0, 1.0);

    chorusPhase += (0.18 + chorus * 0.58) / currentSampleRate;
    if (chorusPhase >= 1.0)
        chorusPhase -= std::floor(chorusPhase);

    const double chorusDelay = chorus <= 0.001 ? 0.0 : 0.006 + chorus * 0.012;
    const double chorusDepth = chorus * 0.006;
    const double lfo = std::sin(2.0 * kPi * chorusPhase);
    const double chorusLeft = chorusDelay > 0.0
        ? readDelay(chorusBufferLeft, chorusWriteIndex, (chorusDelay + lfo * chorusDepth) * currentSampleRate)
        : 0.0;
    const double chorusRight = chorusDelay > 0.0
        ? readDelay(chorusBufferRight, chorusWriteIndex, (chorusDelay * 1.17 - lfo * chorusDepth * 0.85) * currentSampleRate)
        : 0.0;
    if (!chorusBufferLeft.empty())
    {
        chorusBufferLeft[static_cast<std::size_t>(chorusWriteIndex)] = input;
        chorusBufferRight[static_cast<std::size_t>(chorusWriteIndex)] = input;
        chorusWriteIndex = (chorusWriteIndex + 1) % static_cast<int>(chorusBufferLeft.size());
    }

    const double left = input * dryGain + reverbOutLeft * reverbWetGain + toneLeft * echoWetGain + chorusLeft * chorus * 0.34;
    const double right = input * dryGain + reverbOutRight * reverbWetGain + toneRight * echoWetGain + chorusRight * chorus * 0.34;
    const double limitedLeft = softLimit(left);
    const double limitedRight = softLimit(right);
    lastLeft += clampDouble(limitedLeft - lastLeft, -0.42, 0.42);
    lastRight += clampDouble(limitedRight - lastRight, -0.42, 0.42);
    return { static_cast<float>(lastLeft), static_cast<float>(lastRight) };
}

StereoSample OpalineEngine::renderSample()
{
    globalLfoAge += 1.0 / currentSampleRate;
    double mixed = 0.0;

    for (auto& voice : voices)
        mixed += voice.render(patch, pitchBend, pitchBendRange, modWheel, modWheelPitchRange, modWheelAmpRange, globalLfoAge, renderModel);

    voices.erase(std::remove_if(voices.begin(),
                                voices.end(),
                                [](const OpalineVoice& voice) { return !voice.isActive(); }),
                 voices.end());

    const double output = limitAndDeclick(mixed * kOutputGain);
    return processEffects(output);
}

void OpalineEngine::renderBlock(float* left, float* right, const int numSamples)
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto sample = renderSample();
        left[i] = sample.left;
        right[i] = sample.right;
    }
}
} // namespace opaline
