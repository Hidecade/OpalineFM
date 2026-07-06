#include "Engine/Dx21ChipEnvelope.h"

#include <array>
#include <cmath>

namespace dx21
{
namespace
{
// NEW EGは0が最大音量、1023が無音側の10bitインデックスを直接扱う。
constexpr double kEgIndexMax = 1023.0;
constexpr double kEgIndexDbRange = 128.0;
constexpr double kQuietDb = 96.0;
constexpr double kDx21EgTickHz = 3579545.0 / 64.0 / 3.0;
constexpr double kAttackRateScale = 2.0;
constexpr double kDecayRateScale = 1.85;
constexpr double kReleaseRateScale = 3.75;
constexpr double kReleaseRateBias = 2.0;
constexpr int kEgRateSteps = 8;

constexpr std::array<int, 152> kEgInc {
    0, 1, 0, 1, 0, 1, 0, 1,
    0, 1, 0, 1, 1, 1, 0, 1,
    0, 1, 1, 1, 0, 1, 1, 1,
    0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 2, 1, 1, 1, 2,
    1, 2, 1, 2, 1, 2, 1, 2,
    1, 2, 2, 2, 1, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 4, 2, 2, 2, 4,
    2, 4, 2, 4, 2, 4, 2, 4,
    2, 4, 4, 4, 2, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 8, 4, 4, 4, 8,
    4, 8, 4, 8, 4, 8, 4, 8,
    4, 8, 8, 8, 4, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8,
    16, 16, 16, 16, 16, 16, 16, 16,
    0, 0, 0, 0, 0, 0, 0, 0
};

struct EgRateInfo
{
    bool active = false;
    int shift = 0;
    int select = 18 * kEgRateSteps;
};

int midiNoteToKeyScaleCode(const int note)
{
    return clampInt(static_cast<int>(std::round((static_cast<double>(note) - 24.0) / 3.0)), 0, 31);
}

int opmStyleKeyScaleValue(const int note, const int rateScale)
{
    const int scale = clampInt(rateScale, 0, 3);
    return midiNoteToKeyScaleCode(note) >> (scale ^ 3);
}

double dx21EgRate(const int rate, const int note, const int rateScale, const Dx21ChipEnvelope::Stage stage)
{
    const bool release = stage == Dx21ChipEnvelope::Stage::Release;
    const int value = clampInt(rate, 0, release ? 15 : 31);
    if (!release && value <= 0)
        return 0.0;

    const double ksv = static_cast<double>(opmStyleKeyScaleValue(note, rateScale));
    if (release)
        return clampDouble(static_cast<double>(value) * kReleaseRateScale + kReleaseRateBias + ksv, 0.0, 63.0);

    const double scale = stage == Dx21ChipEnvelope::Stage::Attack ? kAttackRateScale : kDecayRateScale;
    return clampDouble(static_cast<double>(value) * scale + ksv, 0.0, 63.0);
}

EgRateInfo egRateInfo(const double rate, const Dx21ChipEnvelope::Stage stage)
{
    const int effective = clampInt(static_cast<int>(std::round(rate)), 0, 63);
    if (effective <= 0)
        return {};

    if (stage == Dx21ChipEnvelope::Stage::Attack && effective >= 62)
        return { true, 0, 17 * kEgRateSteps };

    const int main = effective >> 2;
    const int sub = effective & 3;
    const int shift = main <= 11 ? 11 - main : 0;
    int select = 16;
    if (main <= 11)
        select = sub;
    else if (main == 12)
        select = 4 + sub;
    else if (main == 13)
        select = 8 + sub;
    else if (main == 14)
        select = 12 + sub;

    return { true, shift, select * kEgRateSteps };
}

double egIndexToAmp(const double index)
{
    const double attenuationDb = clampDouble(index, 0.0, kEgIndexMax) * kEgIndexDbRange / kEgIndexMax;
    return std::pow(10.0, -clampDouble(attenuationDb, 0.0, kQuietDb) / 20.0);
}
}

void Dx21ChipEnvelope::reset(const double sampleRate)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    currentStage = Stage::Off;
    egLevel = kEgIndexMax;
    currentRateScale = 0;
    currentNote = 60;
    egCounter = 0;
    egRemainder = 0.0;
}

void Dx21ChipEnvelope::noteOn(const Dx21EnvelopeParams& params, const int rateScale, const int note)
{
    currentParams = params;
    currentRateScale = rateScale;
    currentNote = note;
    egLevel = kEgIndexMax;
    currentStage = Stage::Attack;
    egCounter = 0;
    egRemainder = 0.0;
}

void Dx21ChipEnvelope::noteOff()
{
    if (currentStage != Stage::Off)
        currentStage = Stage::Release;
}

int Dx21ChipEnvelope::egIncrement(const int rate, const Stage stage) const
{
    const auto info = egRateInfo(dx21EgRate(rate, currentNote, currentRateScale, stage), stage);
    if (!info.active)
        return 0;

    if (info.shift > 0 && (egCounter & ((1 << info.shift) - 1)) != 0)
        return 0;

    const int index = info.select + ((egCounter >> info.shift) & 7);
    if (index < 0 || index >= static_cast<int>(kEgInc.size()))
        return 0;

    return kEgInc[static_cast<std::size_t>(index)];
}

double Dx21ChipEnvelope::decay1TargetIndex() const
{
    if (currentParams.decay1Level >= 15)
        return 0.0;

    const double targetDb = static_cast<double>(15 - currentParams.decay1Level) * 3.0;
    return clampDouble(targetDb, 0.0, kEgIndexDbRange) * kEgIndexMax / kEgIndexDbRange;
}

void Dx21ChipEnvelope::advanceAttack()
{
    const int increment = egIncrement(currentParams.attackRate, Stage::Attack);
    if (increment <= 0)
        return;

    // Attackだけは現在値に比例して減るため、直線ではなくカーブした立ち上がりになる。
    if (currentParams.attackRate >= 31)
        egLevel = 0.0;
    else
        egLevel -= (egLevel * static_cast<double>(increment)) / 16.0;

    if (egLevel <= 0.5)
    {
        egLevel = 0.0;
        currentStage = Stage::Decay1;
    }
}

void Dx21ChipEnvelope::advanceDecay(const int rate, const double targetIndex, const Stage nextStage, const Stage rateStage)
{
    const int increment = egIncrement(rate, rateStage);
    if (increment <= 0)
        return;

    egLevel = std::min(targetIndex, egLevel + static_cast<double>(increment));
    if (egLevel >= targetIndex)
    {
        egLevel = targetIndex;
        currentStage = nextStage;
    }
}

void Dx21ChipEnvelope::advanceEgTick()
{
    if (currentStage == Stage::Attack)
        advanceAttack();
    else if (currentStage == Stage::Decay1)
        advanceDecay(currentParams.decay1Rate, decay1TargetIndex(), Stage::Decay2, Stage::Decay1);
    else if (currentStage == Stage::Decay2)
    {
        if (currentParams.decay2Rate > 0)
            advanceDecay(currentParams.decay2Rate, kEgIndexMax, Stage::Off, Stage::Decay2);
    }
    else if (currentStage == Stage::Release)
        advanceDecay(currentParams.releaseRate, kEgIndexMax, Stage::Off, Stage::Release);
}

double Dx21ChipEnvelope::next()
{
    egRemainder += kDx21EgTickHz / currentSampleRate;
    while (egRemainder >= 1.0)
    {
        egRemainder -= 1.0;
        ++egCounter;
        advanceEgTick();
    }

    return egIndexToAmp(std::round(clampDouble(egLevel, 0.0, kEgIndexMax)));
}

bool Dx21ChipEnvelope::isActive() const
{
    return currentStage != Stage::Off;
}
} // namespace dx21
