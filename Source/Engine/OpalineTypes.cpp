#include "Engine/OpalineTypes.h"

#include <algorithm>
#include <cmath>

namespace opaline
{
double clampDouble(const double value, const double low, const double high)
{
    return std::max(low, std::min(high, value));
}

int clampInt(const int value, const int low, const int high)
{
    return std::max(low, std::min(high, value));
}

double midiNoteToFrequency(const double midiNote)
{
    return 440.0 * std::pow(2.0, (midiNote - 69.0) / 12.0);
}

OpalinePatch normalizePatch(const OpalinePatch& patch)
{
    OpalinePatch normalized = patch;
    normalized.algorithm = clampInt(normalized.algorithm, 1, 8);
    normalized.feedback = clampInt(normalized.feedback, 0, 7);
    normalized.transpose = clampInt(normalized.transpose, -24, 24);

    normalized.lfo.speed = clampInt(normalized.lfo.speed, 0, 99);
    normalized.lfo.delay = clampInt(normalized.lfo.delay, 0, 99);
    normalized.lfo.pitchDepth = clampInt(normalized.lfo.pitchDepth, 0, 99);
    normalized.lfo.ampDepth = clampInt(normalized.lfo.ampDepth, 0, 99);
    normalized.lfo.pitchSensitivity = clampInt(normalized.lfo.pitchSensitivity, 0, 7);
    normalized.lfo.ampSensitivity = clampInt(normalized.lfo.ampSensitivity, 0, 3);
    normalized.lfo.wave = clampInt(normalized.lfo.wave, 0, 3);

    normalized.pitchEnvelope.rate1 = clampInt(normalized.pitchEnvelope.rate1, 0, 99);
    normalized.pitchEnvelope.rate2 = clampInt(normalized.pitchEnvelope.rate2, 0, 99);
    normalized.pitchEnvelope.rate3 = clampInt(normalized.pitchEnvelope.rate3, 0, 99);
    normalized.pitchEnvelope.level1 = clampInt(normalized.pitchEnvelope.level1, 0, 99);
    normalized.pitchEnvelope.level2 = clampInt(normalized.pitchEnvelope.level2, 0, 99);
    normalized.pitchEnvelope.level3 = clampInt(normalized.pitchEnvelope.level3, 0, 99);

    normalized.effects.reverb = clampInt(normalized.effects.reverb, 0, 99);
    normalized.effects.mix = clampInt(normalized.effects.mix, 0, 99);
    normalized.effects.tone = clampInt(normalized.effects.tone, 0, 99);
    normalized.effects.chorus = clampInt(normalized.effects.chorus, 0, 99);
    normalized.effects.delay = clampInt(normalized.effects.delay, 0, 99);

    for (auto& op : normalized.operators)
    {
        op.ratioIndex = clampInt(op.ratioIndex, 0, 63);
        op.detune = clampInt(op.detune, -3, 3);
        op.level = clampInt(op.level, 0, 99);
        op.rateScale = clampInt(op.rateScale, 0, 3);
        op.levelScale = clampInt(op.levelScale, 0, 99);
        op.velocity = clampInt(op.velocity, 0, 7);
        op.envelope.attackRate = clampInt(op.envelope.attackRate, 0, 31);
        op.envelope.decay1Rate = clampInt(op.envelope.decay1Rate, 0, 31);
        op.envelope.decay1Level = clampInt(op.envelope.decay1Level, 0, 15);
        op.envelope.decay2Rate = clampInt(op.envelope.decay2Rate, 0, 31);
        op.envelope.releaseRate = clampInt(op.envelope.releaseRate, 0, 15);
    }

    return normalized;
}
} // namespace opaline
