#include "Engine/Dx21PitchEnvelope.h"

#include <algorithm>
#include <cmath>

namespace dx21
{
namespace
{
// PL50を中心に、上下約4オクターブをセント単位で扱う。
constexpr double kPegStepCents = 96.0;
constexpr double kPegRateReferenceCents = 49.0 * kPegStepCents;
constexpr double kPegRate0Seconds = 63.55;
constexpr double kPegRate99Seconds = 0.035;
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

    // DX21のPEGはPL3を初期値として、PR1/PL1からPR2/PL2へ進む。
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

    const double seconds = rateToBaseTimeSeconds(rate) * distance / kPegRateReferenceCents;
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
    return static_cast<double>(value - 50) * kPegStepCents;
}

double Dx21PitchEnvelope::rateToBaseTimeSeconds(const int rate)
{
    const int value = clampInt(rate, 0, 99);
    if (value <= 0)
        return kPegRate0Seconds;
    if (value >= 99)
        return kPegRate99Seconds;

    const double pr = static_cast<double>(value);
    return std::exp(0.0001099 * pr * pr - 0.04875 * pr + 2.6507);
}
} // namespace dx21
