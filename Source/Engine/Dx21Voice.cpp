#include "Engine/Dx21Voice.h"

#include "Engine/Dx21Tables.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace dx21
{
namespace
{
constexpr double kCarrierLevelDbRange = 48.0;
constexpr double kModulatorIndexScale = 1.08;
constexpr double kModulatorIndexBlend = 0.08;
constexpr double kModulatorIndexExponent = 2.2;
constexpr double kModulatorAttackSoftenSeconds = 0.003;
constexpr double kModulatorAttackInitialScale = 0.96;
constexpr double kOpmPhaseSteps = 1048576.0;
constexpr double kOpmPhaseMaxIncrement = kOpmPhaseSteps - 1.0;
constexpr double kOpmSineIndexSteps = 1024.0;
constexpr double kOpmOperatorBusPeak = 8192.0;
constexpr double kOpmBusPhaseGain = 9.0 / (kOpmOperatorBusPeak * kPi / kOpmSineIndexSteps);
constexpr double kOppTlRampSeconds = 0.012;
constexpr double kOppTlSubsteps = 8.0;
constexpr double kOppTlMax = 127.0;
constexpr double kCarrierMixGain = 0.86;
constexpr std::array<double, 8> kDx21PitchSensitivitySemitones {
    2.0,    // PMS=0 uses the DX21 vibrato oscillator path; measured like PMS=5.
    0.125,
    0.25,
    0.5,
    1.0,
    2.0,
    4.0,
    8.0
};

int nextNukedOpmNoiseInjectedBit(std::uint32_t& noiseLfsr, int& noiseBit)
{
    const int injected = noiseLfsr & 1u;
    const bool reset = (noiseLfsr & 0xffffu) == 0u && noiseBit == 0;
    const int feedback = static_cast<int>((noiseLfsr >> 2) & 1u) ^ noiseBit;
    const int newBit = reset ? 1 : feedback;
    noiseBit = static_cast<int>(noiseLfsr & 1u);
    noiseLfsr >>= 1;
    noiseLfsr |= static_cast<std::uint32_t>(newBit) << 15;
    return injected;
}

int nextNukedOpmSampleAndHoldValue(std::uint32_t& noiseLfsr, int& noiseBit)
{
    int value = 0;
    for (int bit = 0; bit < 8; ++bit)
        value = (value << 1) | nextNukedOpmNoiseInjectedBit(noiseLfsr, noiseBit);

    return value;
}

double outputLevelToCarrierAmplitude(const double level)
{
    const double normalized = clampDouble(level, 0.0, 99.0) / 99.0;
    if (normalized <= 0.0)
        return 0.0;
    return std::pow(10.0, -((1.0 - normalized) * kCarrierLevelDbRange) / 20.0);
}

double outputLevelToModulatorIndex(const double level)
{
    const double carrierAmp = outputLevelToCarrierAmplitude(level);
    const double normalized = clampDouble(level, 0.0, 99.0) / 99.0;
    if (normalized <= 0.0)
        return 0.0;

    const double shapedIndex = std::pow(normalized, kModulatorIndexExponent);
    return carrierAmp * kModulatorIndexScale + shapedIndex * kModulatorIndexBlend;
}

double keyboardScaledLevel(const double level, const int levelScale, const int note)
{
    const double scale = clampDouble(static_cast<double>(levelScale), 0.0, 99.0);
    if (scale <= 0.0)
        return level;

    const double highKeyAmount = clampDouble((static_cast<double>(note) - 60.0) / 36.0, 0.0, 1.0);
    return clampDouble(level - highKeyAmount * scale * 0.45, 0.0, 99.0);
}

double operatorAmpModFactor(const double depth, const double shapeValue)
{
    return clampDouble(1.0 - depth * shapeValue, 0.0, 1.0);
}

double modulatorAttackSoftening(const double age)
{
    const double progress = clampDouble(age / kModulatorAttackSoftenSeconds, 0.0, 1.0);
    return kModulatorAttackInitialScale + (1.0 - kModulatorAttackInitialScale) * progress;
}

bool isCarrierOperator(const Algorithm& algorithm, const int opIndex)
{
    for (int i = 0; i < algorithm.carrierCount; ++i)
    {
        if (algorithm.carriers[static_cast<std::size_t>(i)] == opIndex)
            return true;
    }

    return false;
}

double operatorVelocityFactor(const int opVelocity, const int noteVelocity, const bool carrier)
{
    const double amount = clampDouble(static_cast<double>(opVelocity), 0.0, 7.0) / 7.0;
    if (amount <= 0.0)
        return 1.0;

    const double velocity = clampDouble(static_cast<double>(noteVelocity), 1.0, 127.0) / 127.0;
    const double shapedVelocity = std::pow(velocity, 1.35);
    const double minimum = carrier ? 0.20 : 0.32;
    const double maximum = carrier ? 1.85 : 2.55;
    const double fullRangeFactor = minimum + (maximum - minimum) * shapedVelocity;
    return 1.0 + (fullRangeFactor - 1.0) * amount;
}

double opmStylePhaseAdvance(const double frequency, const double sampleRate)
{
    const double increment = std::round(clampDouble(frequency, 0.0, sampleRate * 0.49) * kOpmPhaseSteps / sampleRate);
    return clampDouble(increment, 0.0, kOpmPhaseMaxIncrement) * 2.0 * kPi / kOpmPhaseSteps;
}

double opmPhaseBusToRadians(const double bus)
{
    return bus * kPi / kOpmSineIndexSteps * kOpmBusPhaseGain;
}

double opmFeedbackBusToRadians(const double bus, const int level)
{
    const int feedback = clampInt(level, 0, 7);
    if (feedback <= 0)
        return 0.0;

    const int shift = feedback + 6;
    return bus * std::pow(2.0, static_cast<double>(shift)) * 2.0 * kPi
        / (kOpmSineIndexSteps * 65536.0) * kOpmBusPhaseGain;
}

double levelToOppTlTarget(const double level)
{
    return std::round((1.0 - clampDouble(level, 0.0, 99.0) / 99.0) * kOppTlMax);
}

double oppTlUnitsToLevel(const double units)
{
    return clampDouble((1.0 - clampDouble(units, 0.0, kOppTlMax * kOppTlSubsteps)
                            / (kOppTlMax * kOppTlSubsteps))
                           * 99.0,
                       0.0,
                       99.0);
}

double steppedModDepth(const double normalized, const std::array<double, 9>& table)
{
    if (normalized <= 0.0)
        return 0.0;

    const double scaled = normalized * static_cast<double>(table.size() - 1);
    const auto index = static_cast<std::size_t>(std::floor(scaled));
    const double fraction = scaled - std::floor(scaled);
    const double lower = table[std::min(index, table.size() - 1)];
    const double upper = table[std::min(index + 1, table.size() - 1)];
    return lower + (upper - lower) * std::pow(fraction, 1.35);
}

double opmStylePitchModDepth(const int depth, const int sensitivity)
{
    const double normalized = static_cast<double>(clampInt(depth, 0, 99)) / 99.0;
    const auto sensitivityIndex = static_cast<std::size_t>(clampInt(sensitivity, 0, 7));
    return normalized * kDx21PitchSensitivitySemitones[sensitivityIndex];
}

double opmStyleAmpModDepth(const int depth, const int sensitivity)
{
    static constexpr std::array<double, 9> kDepthTable { 0.0, 0.025, 0.055, 0.11, 0.22, 0.38, 0.58, 0.78, 0.96 };
    const double normalized = static_cast<double>(clampInt(depth, 0, 99)) / 99.0;
    const double normalizedSensitivity = static_cast<double>(clampInt(sensitivity, 0, 3)) / 3.0;
    return normalizedSensitivity * steppedModDepth(normalized, kDepthTable);
}

double lfoDelayFactor(const int delay, const double age)
{
    if (delay <= 0)
        return 1.0;

    const double waitSeconds = 0.25 * std::pow(2.0, static_cast<double>(clampInt(delay, 0, 99)) / 25.0);
    const double highDelay = clampDouble((static_cast<double>(delay) - 75.0) / 24.0, 0.0, 1.0);
    const double fadeSeconds = waitSeconds * (1.0 + 2.0 * highDelay);
    if (age <= waitSeconds)
        return 0.0;

    return std::min(1.0, (age - waitSeconds) / std::max(0.001, fadeSeconds));
}

std::pair<double, double> dx21LfoShape(const double phase, const int wave)
{
    const int index = static_cast<int>(std::floor((phase - std::floor(phase)) * 256.0)) & 255;
    double am = 0.0;
    double pm = 0.0;

    if (wave == 0) // SAW UP
    {
        am = static_cast<double>(255 - index) / 255.0;
        pm = static_cast<double>(index < 128 ? index : index - 255) / 128.0;
    }
    else if (wave == 1) // SQUARE
    {
        am = index < 128 ? 1.0 : 0.0;
        pm = index < 128 ? 1.0 : -1.0;
    }
    else if (wave == 2) // TRIANGLE
    {
        const double amRaw = index < 128 ? 255.0 - static_cast<double>(index) * 2.0
                                         : static_cast<double>(index) * 2.0 - 256.0;
        double pmRaw = 0.0;
        if (index < 64)
            pmRaw = static_cast<double>(index) * 2.0;
        else if (index < 128)
            pmRaw = 255.0 - static_cast<double>(index) * 2.0;
        else if (index < 192)
            pmRaw = 256.0 - static_cast<double>(index) * 2.0;
        else
            pmRaw = static_cast<double>(index) * 2.0 - 511.0;

        am = amRaw / 255.0;
        pm = pmRaw / 128.0;
    }
    else // S/H
    {
        am = 0.5;
        pm = 0.0;
    }

    return { clampDouble(am, 0.0, 1.0), clampDouble(pm, -1.0, 1.0) };
}

std::pair<double, double> dx21PitchLfoShape(const double phase, const int wave, const int sensitivity)
{
    if (clampInt(sensitivity, 0, 7) != 0)
        return dx21LfoShape(phase, wave);

    return dx21LfoShape(phase, 2);
}
}

void Dx21Voice::start(const Dx21Patch& patch, const int newMidiNote, const int velocity, const double sampleRate)
{
    midiNote = newMidiNote;
    noteVelocity = clampInt(velocity, 0, 127);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    ageSeconds = 0.0;
    phases.fill(0.0);
    operatorTlAccumulators.fill(0.0);
    delayedPitchLfo = 0.0;
    sampleAndHoldLfsr = 0;
    sampleAndHoldBit = 0;
    sampleAndHoldCycle = -1;
    sampleAndHoldValue = 128;
    feedbackHistory.fill(0.0);
    failed = false;
    pitchEnvelope.reset(currentSampleRate);
    pitchEnvelope.noteOn(patch.pitchEnvelope);

    for (int i = 0; i < kOperatorCount; ++i)
    {
        operatorOppTlUnits[static_cast<std::size_t>(i)] =
            levelToOppTlTarget(patch.operators[static_cast<std::size_t>(i)].level) * kOppTlSubsteps;
        envelopes[static_cast<std::size_t>(i)].reset(currentSampleRate);
        envelopes[static_cast<std::size_t>(i)].noteOn(patch.operators[static_cast<std::size_t>(i)].envelope,
                                                      patch.operators[static_cast<std::size_t>(i)].rateScale,
                                                      midiNote);
    }
}

void Dx21Voice::release()
{
    for (auto& envelope : envelopes)
        envelope.noteOff();
    pitchEnvelope.noteOff();
}

bool Dx21Voice::isActive() const
{
    if (failed)
        return false;

    for (const auto& envelope : envelopes)
    {
        if (envelope.isActive())
            return true;
    }
    return false;
}

double Dx21Voice::nextOperatorLevel(const int index, const int targetLevel)
{
    const auto opSize = static_cast<std::size_t>(index);
    double current = operatorOppTlUnits[opSize];
    const double target = levelToOppTlTarget(targetLevel);
    const double stepInterval = std::max(1.0, currentSampleRate * kOppTlRampSeconds / (kOppTlMax * kOppTlSubsteps));

    operatorTlAccumulators[opSize] += 1.0;
    const bool match = operatorTlAccumulators[opSize] >= stepInterval;
    bool stepped = false;

    if (match)
    {
        const double value = std::floor(current / kOppTlSubsteps);
        if (value < target)
        {
            current += 1.0;
            stepped = true;
        }
        else if (value > target)
        {
            current -= 1.0;
            stepped = true;
        }
        else
        {
            current = target * kOppTlSubsteps;
            operatorTlAccumulators[opSize] = 0.0;
        }
    }

    if (match && stepped)
        operatorTlAccumulators[opSize] -= stepInterval;

    operatorOppTlUnits[opSize] = clampDouble(current, 0.0, kOppTlMax * kOppTlSubsteps);
    return oppTlUnitsToLevel(operatorOppTlUnits[opSize]);
}

double Dx21Voice::nextPitchModulation(const double pitchLfo)
{
    const double delayed = delayedPitchLfo;
    delayedPitchLfo = pitchLfo;
    return delayed;
}

std::pair<double, double> Dx21Voice::nextSampleAndHoldLfoShape(const double phase)
{
    const int cycle = static_cast<int>(std::floor(phase));
    if (cycle != sampleAndHoldCycle)
    {
        sampleAndHoldValue = nextNukedOpmSampleAndHoldValue(sampleAndHoldLfsr, sampleAndHoldBit);
        sampleAndHoldCycle = cycle;
    }

    const double am = static_cast<double>(sampleAndHoldValue) / 255.0;
    const double pm = static_cast<double>(sampleAndHoldValue) / 127.5 - 1.0;
    return { clampDouble(am, 0.0, 1.0), clampDouble(pm, -1.0, 1.0) };
}

OperatorRender Dx21Voice::renderOperator(const int opIndex,
                                         const Dx21Patch& patch,
                                         const Algorithm& algorithm,
                                         const double baseFrequency,
                                         const double ampDepth,
                                         const double lfoAm,
                                         std::array<bool, kOperatorCount>& computed,
                                         std::array<OperatorRender, kOperatorCount>& outputs)
{
    const auto opSize = static_cast<std::size_t>(opIndex);
    if (computed[opSize])
        return outputs[opSize];

    const auto& op = patch.operators[opSize];
    if (!op.enabled)
    {
        computed[opSize] = true;
        outputs[opSize] = {};
        return {};
    }

    double phaseModulation = 0.0;
    for (int i = 0; i < algorithm.depCounts[opSize]; ++i)
    {
        const int dep = algorithm.deps[opSize][static_cast<std::size_t>(i)];
        phaseModulation += renderOperator(dep, patch, algorithm, baseFrequency, ampDepth, lfoAm, computed, outputs).modulation;
    }

    const double feedback = opIndex == 3
        ? opmFeedbackBusToRadians(feedbackHistory[0] + feedbackHistory[1], patch.feedback)
        : 0.0;

    const double ratio = dx21Ratios()[static_cast<std::size_t>(op.ratioIndex)];
    const double frequency = baseFrequency * ratio + opmStyleDt1FrequencyOffset(baseFrequency, ratio, op.detune, midiNote);
    phases[opSize] += opmStylePhaseAdvance(frequency, currentSampleRate);
    if (phases[opSize] > 2.0 * kPi)
        phases[opSize] -= 2.0 * kPi;

    const double envelopeAmp = envelopes[opSize].next();
    const double level = nextOperatorLevel(opIndex, op.level);
    const double scaledLevel = keyboardScaledLevel(level, op.levelScale, midiNote);
    const bool carrier = isCarrierOperator(algorithm, opIndex);
    const double carrierVelocityFactor = operatorVelocityFactor(op.velocity, noteVelocity, true);
    const double modulatorVelocityFactor = operatorVelocityFactor(op.velocity, noteVelocity, false);
    const double ampMod = op.ampModEnable ? operatorAmpModFactor(ampDepth, lfoAm) : 1.0;
    const double carrierAmp = outputLevelToCarrierAmplitude(scaledLevel) * envelopeAmp * carrierVelocityFactor * ampMod;
    const double modulatorIndex = outputLevelToModulatorIndex(scaledLevel) * envelopeAmp
        * (carrier ? carrierVelocityFactor : modulatorVelocityFactor) * ampMod
        * modulatorAttackSoftening(ageSeconds);

    const double wave = sineLookup(phases[opSize] + opmPhaseBusToRadians(phaseModulation) + feedback);
    const OperatorRender value { wave * carrierAmp, wave * modulatorIndex * kOpmOperatorBusPeak };
    if (opIndex == 3)
    {
        feedbackHistory[1] = feedbackHistory[0];
        feedbackHistory[0] = value.modulation;
    }

    computed[opSize] = true;
    outputs[opSize] = value;
    return value;
}

double Dx21Voice::render(const Dx21Patch& patch,
                         const double pitchBend,
                         const double modWheel,
                         const double globalLfoAge)
{
    ageSeconds += 1.0 / currentSampleRate;

    const auto& algorithm = dx21Algorithms()[static_cast<std::size_t>(patch.algorithm - 1)];
    const double lfoAge = patch.lfo.sync ? ageSeconds : globalLfoAge;
    const double lfoPhase = dx21LfoSpeedToHz(patch.lfo.speed) * lfoAge;
    const auto lfo = patch.lfo.wave == 3 ? nextSampleAndHoldLfoShape(lfoPhase)
                                         : dx21LfoShape(lfoPhase, patch.lfo.wave);
    const auto pitchLfoShape = clampInt(patch.lfo.pitchSensitivity, 0, 7) == 0
        ? dx21LfoShape(lfoPhase, 2)
        : lfo;
    const double delay = lfoDelayFactor(patch.lfo.delay, ageSeconds);
    const double pitchLfo = opmStylePitchModDepth(patch.lfo.pitchDepth, patch.lfo.pitchSensitivity) * delay
        * (0.35 + modWheel * 0.65) * pitchLfoShape.second;
    const double ampDepth = opmStyleAmpModDepth(patch.lfo.ampDepth, patch.lfo.ampSensitivity) * delay;
    const double appliedPitchLfo = nextPitchModulation(pitchLfo);
    const double pitchEnvelopeSemitones = pitchEnvelope.nextSemitones();
    const double bendSemitones = clampDouble(pitchBend, -1.0, 1.0) * 2.0;
    const double baseFrequency = midiNoteToFrequency(static_cast<double>(midiNote + patch.transpose))
        * std::pow(2.0, (bendSemitones + appliedPitchLfo + pitchEnvelopeSemitones) / 12.0);

    std::array<bool, kOperatorCount> computed {};
    std::array<OperatorRender, kOperatorCount> outputs {};
    double sum = 0.0;

    for (int i = 0; i < algorithm.carrierCount; ++i)
    {
        const int carrier = algorithm.carriers[static_cast<std::size_t>(i)];
        sum += renderOperator(carrier, patch, algorithm, baseFrequency, ampDepth, lfo.first, computed, outputs).audio;
    }

    if (!std::isfinite(sum))
    {
        failed = true;
        feedbackHistory.fill(0.0);
        return 0.0;
    }

    return algorithm.carrierCount > 0 ? (sum / std::sqrt(static_cast<double>(algorithm.carrierCount))) * kCarrierMixGain : 0.0;
}
} // namespace dx21
