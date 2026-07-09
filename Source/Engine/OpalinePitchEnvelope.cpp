#include "Engine/OpalinePitchEnvelope.h"

#include <algorithm>
#include <cmath>

namespace opaline
{
namespace
{
// PL50を中心に、実機録音から得た上下方向のテーブルを使う。
constexpr double kPegRateReferenceCents = 4800.0;
constexpr double kPegRate0Seconds = 63.55;
constexpr double kPegRate99Seconds = 0.012;
constexpr double kPegLowerLevelAnchorCents[] {
    -4800.0,
    -4052.84,
    -3301.35,
    -2700.85,
    -2201.97,
    -1706.67,
    -1199.97,
    -752.98,
    -498.94,
    -258.53,
    0.0
};
constexpr double kPegUpperLevelCents[] {
    0.0,
    50.92,
    102.87,
    149.92,
    203.15,
    251.36,
    300.72,
    350.55,
    401.06,
    452.13,
    504.40,
    550.31,
    603.74,
    650.91,
    698.46,
    803.70,
    904.28,
    998.80,
    1105.37,
    1200.0,
    1297.44,
    1403.17,
    1500.71,
    1601.08,
    1704.42,
    1796.50,
    1906.16,
    2003.71,
    2104.28,
    2207.92,
    2349.70,
    2497.47,
    2603.16,
    2700.70,
    2801.08,
    2904.42,
    2996.49,
    3106.17,
    3203.72,
    3304.28,
    3451.58,
    3605.12,
    3752.23,
    3902.12,
    4052.19,
    4196.53,
    4362.73,
    4504.27,
    4651.56,
    4800.0
};
constexpr double kPegHighRateSeconds[] {
    0.270,
    0.258,
    0.240,
    0.230,
    0.216,
    0.204,
    0.179,
    0.175,
    0.155
};

double interpolateLowerLevelCents(const int value)
{
    const int anchor = value / 5;
    const int nextAnchor = std::min(anchor + 1, 10);
    const double fraction = static_cast<double>(value - anchor * 5) / 5.0;
    const double start = kPegLowerLevelAnchorCents[static_cast<std::size_t>(anchor)];
    const double end = kPegLowerLevelAnchorCents[static_cast<std::size_t>(nextAnchor)];
    return start + (end - start) * fraction;
}
}

void OpalinePitchEnvelope::reset(const double sampleRate)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    currentParams = {};
    currentStage = Stage::Off;
    currentCents = 0.0;
    targetCents = 0.0;
    centsPerSample = 0.0;
}

void OpalinePitchEnvelope::noteOn(const OpalinePitchEnvelopeParams& params)
{
    currentParams = params;

    // compatibleのPEGはPL3を初期値として、PR1/PL1からPR2/PL2へ進む。
    currentCents = levelToCents(currentParams.level3);
    startSegment(Stage::Stage1, currentParams.level1, currentParams.rate1);
}

void OpalinePitchEnvelope::noteOff()
{
    if (currentStage != Stage::Off && currentStage != Stage::Finished)
        startSegment(Stage::Release, currentParams.level3, currentParams.rate3);
}

double OpalinePitchEnvelope::nextSemitones()
{
    if (currentStage == Stage::Stage1 || currentStage == Stage::Stage2 || currentStage == Stage::Release)
        advanceSegment();
    else if (currentStage == Stage::Sustain)
        currentCents = levelToCents(currentParams.level2);

    return currentCents / 100.0;
}

void OpalinePitchEnvelope::startSegment(const Stage stage, const int targetLevel, const int rate)
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

void OpalinePitchEnvelope::advanceSegment()
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

double OpalinePitchEnvelope::levelToCents(const int level)
{
    const int value = clampInt(level, 0, 99);
    if (value >= 50)
        return kPegUpperLevelCents[static_cast<std::size_t>(value - 50)];

    return interpolateLowerLevelCents(value);
}

double OpalinePitchEnvelope::rateToBaseTimeSeconds(const int rate)
{
    const int value = clampInt(rate, 0, 99);
    if (value <= 0)
        return kPegRate0Seconds;
    if (value >= 99)
        return kPegRate99Seconds;
    if (value >= 90)
        return kPegHighRateSeconds[static_cast<std::size_t>(value - 90)];

    const double pr = static_cast<double>(value);
    return std::exp(0.0001099 * pr * pr - 0.04875 * pr + 2.6507);
}
} // namespace opaline
