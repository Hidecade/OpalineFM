#include "Engine/Dx21PitchEnvelope.h"

#include <algorithm>
#include <cmath>

namespace dx21
{
namespace
{
constexpr double kPegRangeCents = 4800.0;
constexpr double kPegMinTimeSeconds = 0.005;
constexpr double kPegMaxTimeSeconds = 20.0;
}

void Dx21PitchEnvelope::reset(const double sampleRate)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    currentParams = {};
    currentStage = Stage::Off;
    currentCents = 0.0;
    targetCents = 0.0;
    centsPerSample = 0.0;
}

void Dx21PitchEnvelope::noteOn(const Dx21PitchEnvelopeParams& params)
{
    currentParams = params;
    currentCents = levelToCents(currentParams.level3);
    startSegment(Stage::Stage1, currentParams.level1, currentParams.rate1);
}

void Dx21PitchEnvelope::noteOff()
{
    if (currentStage != Stage::Off && currentStage != Stage::Finished)
        startSegment(Stage::Release, currentParams.level3, currentParams.rate3);
}

double Dx21PitchEnvelope::nextSemitones()
{
    if (currentStage == Stage::Stage1 || currentStage == Stage::Stage2 || currentStage == Stage::Release)
        advanceSegment();
    else if (currentStage == Stage::Sustain)
        currentCents = levelToCents(currentParams.level2);

    return currentCents / 100.0;
}

void Dx21PitchEnvelope::startSegment(const Stage stage, const int targetLevel, const int rate)
{
    currentStage = stage;
    targetCents = levelToCents(targetLevel);
    const double distance = std::abs(targetCents - currentCents);
    if (distance <= 0.000001)
    {
        currentCents = targetCents;
        centsPerSample = 0.0;
        if (stage == Stage::Stage1)
            startSegment(Stage::Stage2, currentParams.level2, currentParams.rate2);
        else if (stage == Stage::Stage2)
            currentStage = Stage::Sustain;
        else if (stage == Stage::Release)
            currentStage = Stage::Finished;
        return;
    }

    const double seconds = rateToBaseTimeSeconds(rate) * distance / kPegRangeCents;
    const double samples = std::max(1.0, seconds * currentSampleRate);
    centsPerSample = distance / samples;
}

void Dx21PitchEnvelope::advanceSegment()
{
    if (currentCents < targetCents)
        currentCents = std::min(targetCents, currentCents + centsPerSample);
    else
        currentCents = std::max(targetCents, currentCents - centsPerSample);

    if (currentCents != targetCents)
        return;

    if (currentStage == Stage::Stage1)
        startSegment(Stage::Stage2, currentParams.level2, currentParams.rate2);
    else if (currentStage == Stage::Stage2)
        currentStage = Stage::Sustain;
    else if (currentStage == Stage::Release)
        currentStage = Stage::Finished;
}

double Dx21PitchEnvelope::levelToCents(const int level)
{
    const int value = clampInt(level, 0, 99);
    if (value >= 50)
        return static_cast<double>(value - 50) * kPegRangeCents / 49.0;

    return static_cast<double>(value - 50) * kPegRangeCents / 50.0;
}

double Dx21PitchEnvelope::rateToBaseTimeSeconds(const int rate)
{
    const double normalized = static_cast<double>(99 - clampInt(rate, 0, 99)) / 99.0;
    return kPegMinTimeSeconds * std::pow(kPegMaxTimeSeconds / kPegMinTimeSeconds, normalized);
}
} // namespace dx21
