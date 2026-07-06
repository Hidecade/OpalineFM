#include "Engine/Dx21Envelope.h"

#include <array>
#include <cmath>

namespace dx21
{
namespace
{
constexpr double kQuietDb = 96.0;
constexpr double kEgIndexDbRange = 128.0;
constexpr double kEgIndexMax = 1023.0;
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

double attenuationDbToEgIndex(const double db)
{
    return clampDouble(db, 0.0, kEgIndexDbRange) * kEgIndexMax / kEgIndexDbRange;
}

double egIndexToAttenuationDb(const double index)
{
    return clampDouble(index, 0.0, kEgIndexMax) * kEgIndexDbRange / kEgIndexMax;
}

double attenuationDbToAmp(const double db)
{
    return std::pow(10.0, -clampDouble(db, 0.0, kQuietDb) / 20.0);
}

double dx21EgRate(const int rate, const int note, const int rateScale, const Dx21Envelope::Stage stage)
{
    const bool release = stage == Dx21Envelope::Stage::Release;
    const int value = clampInt(rate, 0, release ? 15 : 31);
    if (!release && value <= 0)
        return 0.0;

    const double ksv = static_cast<double>(opmStyleKeyScaleValue(note, rateScale));
    if (release)
        return clampDouble(static_cast<double>(value) * kReleaseRateScale + kReleaseRateBias + ksv, 0.0, 63.0);

    const double scale = stage == Dx21Envelope::Stage::Attack ? kAttackRateScale : kDecayRateScale;
    return clampDouble(static_cast<double>(value) * scale + ksv, 0.0, 63.0);
}

EgRateInfo egRateInfo(const double rate, const Dx21Envelope::Stage stage)
{
    const int effective = clampInt(static_cast<int>(std::round(rate)), 0, 63);
    if (effective <= 0)
        return {};

    if (stage == Dx21Envelope::Stage::Attack && effective >= 62)
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
}

void Dx21Envelope::reset(const double sampleRate)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    currentStage = Stage::Off;
    attenuationDb = kQuietDb;
    currentRateScale = 0;
    currentNote = 60;
    egCounter = 0;
    egRemainder = 0.0;
}

void Dx21Envelope::noteOn(const Dx21EnvelopeParams& params, const int rateScale, const int note)
{
    currentParams = params;
    currentRateScale = rateScale;
    currentNote = note;
    attenuationDb = kQuietDb;
    currentStage = Stage::Attack;
    egCounter = 0;
    egRemainder = 0.0;
}

void Dx21Envelope::noteOff()
{
    if (currentStage != Stage::Off)
        currentStage = Stage::Release;
}

int Dx21Envelope::egIncrement(const int rate, const Stage stage) const
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

void Dx21Envelope::advanceAttack()
{
    const int increment = egIncrement(currentParams.attackRate, Stage::Attack);
    if (increment <= 0)
        return;

    const double index = attenuationDbToEgIndex(attenuationDb);
    attenuationDb = egIndexToAttenuationDb(index - (index * static_cast<double>(increment)) / 16.0);
}

void Dx21Envelope::advanceDecay(const int rate, const double targetDb, const Stage nextStage, const Stage rateStage)
{
    const int increment = egIncrement(rate, rateStage);
    if (increment <= 0)
        return;

    const double currentIndex = attenuationDbToEgIndex(attenuationDb);
    const double targetIndex = attenuationDbToEgIndex(targetDb);
    const double nextIndex = std::min(targetIndex, currentIndex + static_cast<double>(increment));
    attenuationDb = egIndexToAttenuationDb(nextIndex);
    if (nextIndex >= targetIndex)
    {
        attenuationDb = targetDb;
        currentStage = nextStage;
    }
}

void Dx21Envelope::advanceEgTick(const double targetD1Db)
{
    if (currentStage == Stage::Attack)
    {
        advanceAttack();
        if (attenuationDb <= 0.05)
        {
            attenuationDb = 0.0;
            currentStage = Stage::Decay1;
        }
    }
    else if (currentStage == Stage::Decay1)
    {
        advanceDecay(currentParams.decay1Rate, targetD1Db, Stage::Decay2, Stage::Decay1);
    }
    else if (currentStage == Stage::Decay2)
    {
        if (currentParams.decay2Rate > 0)
            advanceDecay(currentParams.decay2Rate, kQuietDb, Stage::Off, Stage::Decay2);
    }
    else if (currentStage == Stage::Release)
    {
        advanceDecay(currentParams.releaseRate, kQuietDb, Stage::Off, Stage::Release);
    }
}

double Dx21Envelope::decay1LevelDb() const
{
    if (currentParams.decay1Level >= 15)
        return 0.0;
    return static_cast<double>(15 - currentParams.decay1Level) * 3.0;
}

double Dx21Envelope::next()
{
    const double targetD1Db = decay1LevelDb();

    egRemainder += kDx21EgTickHz / currentSampleRate;
    while (egRemainder >= 1.0)
    {
        egRemainder -= 1.0;
        ++egCounter;
        advanceEgTick(targetD1Db);
    }

    return attenuationDbToAmp(attenuationDb);
}

bool Dx21Envelope::isActive() const
{
    return currentStage != Stage::Off;
}
} // namespace dx21
