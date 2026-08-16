#include "Engine/OpalineEngine.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace opaline
{
namespace
{
// Global output trim and maximum delay lengths used by the effects.
constexpr double kOutputGain = 0.38;
constexpr double kLimiterThreshold = 0.86;
constexpr double kLimiterCeiling = 0.96;
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

    // Rebuild effect buffers when the sample rate changes.
    stereoDelay.prepare(currentSampleRate);
    chorusBufferLeft.assign(static_cast<std::size_t>(std::ceil(currentSampleRate * kMaxChorusSeconds)) + 4, 0.0);
    chorusBufferRight.assign(chorusBufferLeft.size(), 0.0);
    static constexpr std::array<double, 4> kReverbTimes { 0.0473, 0.0599, 0.0731, 0.0897 };
    for (int i = 0; i < 4; ++i)
    {
        const auto length = static_cast<std::size_t>(std::ceil(currentSampleRate * kReverbTimes[static_cast<std::size_t>(i)])) + 1;
        reverbBufferLeft[static_cast<std::size_t>(i)].assign(length, 0.0);
        reverbBufferRight[static_cast<std::size_t>(i)].assign(length, 0.0);
    }
    voices.clear();
    voices.reserve(static_cast<std::size_t>(maxVoiceCount));
    scopeCycleSamples.fill(0.0f);
    scopeCycleSampleCount = 0;
    scopePhase = 0.0;
    smoothedScopeLevel = 0.0;
    for (auto& sample : publishedScopeWaveform)
        sample.store(0.0f, std::memory_order_relaxed);
    publishedScopeLevel.store(0.0f, std::memory_order_relaxed);
    updateEffectParameters();
    panic();
}

void OpalineEngine::setPatch(const OpalinePatch& newPatch)
{
    const bool wetParametersWereZero = effectWetParametersZero;
    patch = normalizePatch(newPatch);
    updateEffectParameters();
    if (effectWetParametersZero && !wetParametersWereZero)
        resetEffects();
}

void OpalineEngine::setVoiceLimit(const int maxVoices)
{
    const int reservedVoices = std::max(1, static_cast<int>(voices.capacity()));
    maxVoiceCount = clampInt(maxVoices, 1, reservedVoices);
    while (static_cast<int>(voices.size()) > maxVoiceCount)
        voices.erase(voices.begin());
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
        scopeCycleSampleCount = 0;
        scopePhase = 0.0;
        return;
    }

    if (monoMode)
        voices.clear();
    voices.erase(std::remove_if(voices.begin(),
                                voices.end(),
                                [safeNote](const OpalineVoice& voice) { return voice.note() == safeNote; }),
                 voices.end());

    if (static_cast<int>(voices.size()) >= maxVoiceCount)
        voices.erase(voices.begin());

    OpalineVoice voice;
    const bool usePortamento = fullPortamento || fingeredPortamento;
    const int fromNote = usePortamento ? lastPlayedNote : -1;
    voice.start(patch, safeNote, clampInt(velocity, 0, 127), currentSampleRate, renderModel,
                fromNote, portamentoSecondsForValue(portamento));
    static constexpr std::array<double, 8> kStereoPositions {
        -1.0, 1.0, -0.42, 0.42, -0.72, 0.72, -0.18, 0.18
    };
    voice.setStereoPosition(kStereoPositions[stereoVoiceCounter++ % kStereoPositions.size()]);
    lastPlayedNote = safeNote;
    voices.push_back(voice);
    scopeCycleSampleCount = 0;
    scopePhase = 0.0;

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
    lastLeft = 0.0;
    lastRight = 0.0;
    resetEffects();
}

void OpalineEngine::resetEffects()
{
    stereoDelay.reset();
    std::fill(chorusBufferLeft.begin(), chorusBufferLeft.end(), 0.0);
    std::fill(chorusBufferRight.begin(), chorusBufferRight.end(), 0.0);
    for (auto& buffer : reverbBufferLeft)
        std::fill(buffer.begin(), buffer.end(), 0.0);
    for (auto& buffer : reverbBufferRight)
        std::fill(buffer.begin(), buffer.end(), 0.0);
    reverbDampingLeft.fill(0.0);
    reverbDampingRight.fill(0.0);
    reverbWriteIndices.fill(0);
    chorusWriteIndex = 0;
    chorusPhase = 0.0;
    autoPanPhase = 0.0;
    autoPanHeldRandom = 0.0;
    autoPanRandomState = 0x714ac3d9U;
}

void OpalineEngine::updateEffectParameters()
{
    const auto& fx = patch.effects;
    const double reverbCharacter = static_cast<double>(fx.reverb) / 99.0;
    const double legacyReverbMix = static_cast<double>(fx.mix) / 99.0;
    const double delayTime = static_cast<double>(fx.delay) / 99.0;
    const double legacyDelayMix = static_cast<double>(fx.echoMix) / 99.0;
    effectReverb = fx.reverbMode == 0 ? 0.0 : reverbCharacter;
    effectReverbMix = fx.reverbMode == 0 ? 0.0 : legacyReverbMix;
    effectEchoMix = legacyDelayMix;
    effectTone = static_cast<double>(fx.tone) / 99.0;
    autoPanMode = fx.panMode;
    effectChorus = autoPanMode == 4 ? static_cast<double>(fx.chorus) / 99.0 : 0.0;
    effectDelay = delayTime;
    // Aureline AutoPan: a sine LFO sweeps an equal-power pan law around the
    // stereo centre. The logarithmic rate range is 0.05--20 Hz.
    const double normalizedPanRate = static_cast<double>(fx.panRate) / 99.0;
    autoPanRateHz = 0.05 * std::pow(400.0, normalizedPanRate);
    autoPanDepth = autoPanMode == 4 ? 0.0 : static_cast<double>(fx.panDepth) / 99.0;

    const double wetAmount = clampDouble(effectReverbMix + effectChorus * 0.25, 0.0, 1.0);
    effectDryGain = 1.0 - wetAmount * 0.55;
    effectReverbWetGain = effectReverbMix * (0.18 + effectReverb * 0.82);
    effectEchoWetGain = effectEchoMix;
    effectReverbFeedback = 0.48 + effectReverb * 0.40;
    effectReverbDamping = 0.08 + effectTone * 0.30;
    stereoDelayParams.mode = static_cast<StereoDelayMode>(fx.delayMode);
    stereoDelayParams.timeSeconds = 0.01 + effectDelay * 1.99;
    stereoDelayParams.spread = 0.75;
    stereoDelayParams.feedback = effectDelay > 0.0
        ? 0.10 + effectDelay * 0.50 : 0.0;
    stereoDelayParams.tone = effectTone;
    stereoDelayParams.mix = effectEchoMix;
    effectChorusPhaseIncrement = (0.18 + effectChorus * 0.58) / currentSampleRate;
    effectChorusDelay = effectChorus <= 0.001 ? 0.0 : 0.006 + effectChorus * 0.012;
    effectChorusDepth = effectChorus * 0.006;
    effectWetParametersZero = effectReverb <= 0.0
        && effectChorus <= 0.0
        && effectDelay <= 0.0;
    effectOutputDryOnly = effectReverbWetGain == 0.0
        && effectEchoWetGain == 0.0
        && effectChorus == 0.0;
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

StereoSample OpalineEngine::processEffects(const double inputLeft, const double inputRight)
{
    const auto applyAutoPan = [this](double left, double right)
    {
        if (autoPanDepth <= 0.0)
            return std::array<double, 2> { left, right };

        const double oldPhase = autoPanPhase;
        autoPanPhase += autoPanRateHz / currentSampleRate;
        if (autoPanPhase >= 1.0)
            autoPanPhase -= std::floor(autoPanPhase);
        double wave = 0.0;
        switch (autoPanMode)
        {
            case 1: wave = 1.0 - 4.0 * std::abs(autoPanPhase - 0.5); break;
            case 2: wave = autoPanPhase < 0.5 ? 1.0 : -1.0; break;
            case 3:
                if (autoPanPhase < oldPhase)
                {
                    autoPanRandomState ^= autoPanRandomState << 13;
                    autoPanRandomState ^= autoPanRandomState >> 17;
                    autoPanRandomState ^= autoPanRandomState << 5;
                    autoPanHeldRandom = static_cast<double>(autoPanRandomState & 0xffffU)
                        / 32767.5 - 1.0;
                }
                wave = autoPanHeldRandom;
                break;
            case 0:
            default: wave = std::sin(2.0 * kPi * autoPanPhase); break;
        }
        const double pan = wave * autoPanDepth;
        const auto angle = (pan + 1.0) * 0.78539816339744830962;
        return std::array<double, 2> {
            left * std::cos(angle) * 1.4142135623730951,
            right * std::sin(angle) * 1.4142135623730951
        };
    };

    if (!effectsEnabled)
    {
        lastLeft += clampDouble(inputLeft - lastLeft, -0.42, 0.42);
        lastRight += clampDouble(inputRight - lastRight, -0.42, 0.42);
        return { static_cast<float>(lastLeft), static_cast<float>(lastRight) };
    }

    if (effectWetParametersZero)
    {
        const auto panned = applyAutoPan(inputLeft, inputRight);
        const double limitedLeft = softLimit(panned[0]);
        const double limitedRight = softLimit(panned[1]);
        lastLeft += clampDouble(limitedLeft - lastLeft, -0.42, 0.42);
        lastRight += clampDouble(limitedRight - lastRight, -0.42, 0.42);
        return { static_cast<float>(lastLeft), static_cast<float>(lastRight) };
    }

    const double input = (inputLeft + inputRight) * 0.5;

    std::array<double, 4> reverbTapsLeft {};
    std::array<double, 4> reverbTapsRight {};
    for (int i = 0; i < 4; ++i)
    {
        const auto line = static_cast<std::size_t>(i);
        auto& leftBuffer = reverbBufferLeft[line];
        auto& rightBuffer = reverbBufferRight[line];
        if (leftBuffer.empty() || rightBuffer.empty())
            continue;

        int& index = reverbWriteIndices[line];
        index %= static_cast<int>(leftBuffer.size());
        const auto bufferIndex = static_cast<std::size_t>(index);
        reverbDampingLeft[line] += (leftBuffer[bufferIndex] - reverbDampingLeft[line]) * effectReverbDamping;
        reverbDampingRight[line] += (rightBuffer[bufferIndex] - reverbDampingRight[line]) * effectReverbDamping;
        reverbTapsLeft[line] = reverbDampingLeft[line];
        reverbTapsRight[line] = reverbDampingRight[line];
    }

    const auto diffuse = [](const std::array<double, 4>& taps)
    {
        return std::array<double, 4> {
            (taps[0] + taps[1] + taps[2] + taps[3]) * 0.5,
            (taps[0] - taps[1] + taps[2] - taps[3]) * 0.5,
            (taps[0] + taps[1] - taps[2] - taps[3]) * 0.5,
            (taps[0] - taps[1] - taps[2] + taps[3]) * 0.5
        };
    };
    const auto diffusedLeft = diffuse(reverbTapsLeft);
    const auto diffusedRight = diffuse(reverbTapsRight);
    static constexpr std::array<double, 4> kInputLeft { 0.58, 0.49, 0.40, 0.31 };
    static constexpr std::array<double, 4> kInputRight { 0.31, 0.40, 0.49, 0.58 };
    for (int i = 0; i < 4; ++i)
    {
        const auto line = static_cast<std::size_t>(i);
        auto& leftBuffer = reverbBufferLeft[line];
        auto& rightBuffer = reverbBufferRight[line];
        if (leftBuffer.empty() || rightBuffer.empty())
            continue;

        int& index = reverbWriteIndices[line];
        const auto bufferIndex = static_cast<std::size_t>(index);
        const auto crossLine = static_cast<std::size_t>((i + 1) % 4);
        leftBuffer[bufferIndex] = input * kInputLeft[line]
            + effectReverbFeedback * (diffusedLeft[line] * 0.88 + diffusedRight[crossLine] * 0.12);
        rightBuffer[bufferIndex] = input * kInputRight[line]
            + effectReverbFeedback * (diffusedRight[line] * 0.88 + diffusedLeft[crossLine] * 0.12);
        index = (index + 1) % static_cast<int>(leftBuffer.size());
    }

    const double reverbOutLeft = effectWetParametersZero ? 0.0
        : (reverbTapsLeft[0] + reverbTapsLeft[1]
           - reverbTapsLeft[2] + reverbTapsLeft[3]) * 0.32 * effectReverb;
    const double reverbOutRight = effectWetParametersZero ? 0.0
        : (reverbTapsRight[0] - reverbTapsRight[1]
           + reverbTapsRight[2] + reverbTapsRight[3]) * 0.32 * effectReverb;

    chorusPhase += effectChorusPhaseIncrement;
    if (chorusPhase >= 1.0)
        chorusPhase -= std::floor(chorusPhase);

    double chorusLeft = 0.0;
    double chorusRight = 0.0;
    if (effectChorusDelay > 0.0)
    {
        const double lfo = std::sin(2.0 * kPi * chorusPhase);
        chorusLeft = readDelay(chorusBufferLeft, chorusWriteIndex,
                               (effectChorusDelay + lfo * effectChorusDepth) * currentSampleRate);
        chorusRight = readDelay(chorusBufferRight, chorusWriteIndex,
                                (effectChorusDelay * 1.17 - lfo * effectChorusDepth * 0.85) * currentSampleRate);
    }
    if (!chorusBufferLeft.empty())
    {
        chorusBufferLeft[static_cast<std::size_t>(chorusWriteIndex)] = inputLeft;
        chorusBufferRight[static_cast<std::size_t>(chorusWriteIndex)] = inputRight;
        chorusWriteIndex = (chorusWriteIndex + 1) % static_cast<int>(chorusBufferLeft.size());
    }

    double left = effectOutputDryOnly ? inputLeft
        : inputLeft * effectDryGain + reverbOutLeft * effectReverbWetGain
            + chorusLeft * effectChorus * 0.34;
    double right = effectOutputDryOnly ? inputRight
        : inputRight * effectDryGain + reverbOutRight * effectReverbWetGain
            + chorusRight * effectChorus * 0.34;
    const auto delayed = stereoDelay.process(
        { static_cast<float>(left), static_cast<float>(right) },
        stereoDelayParams);
    left = delayed.left;
    right = delayed.right;
    const auto panned = applyAutoPan(left, right);
    left = panned[0];
    right = panned[1];
    const double limitedLeft = softLimit(left);
    const double limitedRight = softLimit(right);
    lastLeft += clampDouble(limitedLeft - lastLeft, -0.42, 0.42);
    lastRight += clampDouble(limitedRight - lastRight, -0.42, 0.42);
    return { static_cast<float>(lastLeft), static_cast<float>(lastRight) };
}

StereoSample OpalineEngine::renderSample()
{
    globalLfoAge += 1.0 / currentSampleRate;
    double mixedLeft = 0.0;
    double mixedRight = 0.0;
    double scopeVoiceSample = 0.0;
    double scopeVoiceFrequency = publishedScopeFrequency.load(std::memory_order_relaxed);
    bool scopeVoiceActive = false;

    std::size_t activeVoiceCount = 0;
    for (std::size_t voiceIndex = 0; voiceIndex < voices.size(); ++voiceIndex)
    {
        auto& voice = voices[voiceIndex];
        const double voiceSample =
            voice.render(patch, pitchBend, pitchBendRange, modWheel, modWheelPitchRange,
                         modWheelAmpRange, globalLfoAge, renderModel);
        mixedLeft += voiceSample;
        mixedRight += voiceSample;
        if (voice.note() == lastPlayedNote)
        {
            scopeVoiceSample = voiceSample * kOutputGain;
            scopeVoiceFrequency = voice.lastBaseFrequencyHz();
            scopeVoiceActive = voice.isActive();
        }
        if (!voice.isActive())
            continue;

        if (activeVoiceCount != voiceIndex)
            voices[activeVoiceCount] = std::move(voice);
        ++activeVoiceCount;
    }
    voices.resize(activeVoiceCount);
    updateVoiceScope(scopeVoiceSample, scopeVoiceFrequency, scopeVoiceActive);

    const double channelGain = patch.effects.muted ? 0.0
        : static_cast<double>(patch.effects.volume) / 99.0;
    const double outputLeft = softLimit(mixedLeft * kOutputGain * channelGain);
    const double outputRight = softLimit(mixedRight * kOutputGain * channelGain);
    return processEffects(outputLeft, outputRight);
}

void OpalineEngine::updateVoiceScope(const double sample,
                                     const double frequency,
                                     const bool voiceActive)
{
    const double safeSample = std::isfinite(sample) ? sample : 0.0;
    const double targetLevel = voiceActive
        ? clampDouble(std::abs(safeSample) * 5.0, 0.0, 1.0) : 0.0;
    const double response = targetLevel > smoothedScopeLevel ? 0.025 : 0.0012;
    smoothedScopeLevel += (targetLevel - smoothedScopeLevel) * response;
    if (!voiceActive && smoothedScopeLevel < 0.001)
        smoothedScopeLevel = 0.0;
    publishedScopeLevel.store(static_cast<float>(smoothedScopeLevel),
                              std::memory_order_release);

    if (!voiceActive || !std::isfinite(frequency) || frequency <= 0.0)
        return;

    publishedScopeFrequency.store(frequency, std::memory_order_release);
    if (scopeCycleSampleCount < static_cast<int>(scopeCycleSamples.size()))
        scopeCycleSamples[static_cast<std::size_t>(scopeCycleSampleCount++)] =
            static_cast<float>(safeSample);

    scopePhase += frequency / currentSampleRate;
    // Some FM ratios (notably 0.5) produce alternating A/B shapes on
    // consecutive fundamental cycles. Capture two cycles as one stable block.
    constexpr double scopeBlockCycles = 2.0;
    if (scopePhase < scopeBlockCycles)
        return;

    scopePhase = std::fmod(scopePhase, scopeBlockCycles);
    if (scopeCycleSampleCount >= 2)
    {
        double average = 0.0;
        for (int i = 0; i < scopeCycleSampleCount; ++i)
            average += scopeCycleSamples[static_cast<std::size_t>(i)];
        average /= static_cast<double>(scopeCycleSampleCount);

        double peak = 0.0;
        for (int i = 0; i < scopeCycleSampleCount; ++i)
            peak = std::max(peak, std::abs(
                static_cast<double>(scopeCycleSamples[static_cast<std::size_t>(i)]) - average));
        const double gain = peak > 1.0e-9 ? 0.98 / peak : 0.0;
        for (std::size_t point = 0; point < publishedScopeWaveform.size(); ++point)
        {
            const double position = static_cast<double>(scopeCycleSampleCount - 1)
                * static_cast<double>(point)
                / static_cast<double>(publishedScopeWaveform.size() - 1);
            const int index = std::min(scopeCycleSampleCount - 2,
                                       static_cast<int>(position));
            const double fraction = position - static_cast<double>(index);
            const double first = scopeCycleSamples[static_cast<std::size_t>(index)] - average;
            const double second = scopeCycleSamples[static_cast<std::size_t>(index + 1)] - average;
            publishedScopeWaveform[point].store(
                static_cast<float>((first + (second - first) * fraction) * gain),
                std::memory_order_release);
        }
    }
    scopeCycleSampleCount = 0;
}

std::array<float, 256> OpalineEngine::scopeWaveformSnapshot() const
{
    std::array<float, 256> snapshot {};
    for (std::size_t i = 0; i < snapshot.size(); ++i)
        snapshot[i] = publishedScopeWaveform[i].load(std::memory_order_acquire);
    return snapshot;
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
