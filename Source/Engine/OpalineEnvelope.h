#pragma once

#include "Engine/OpalineTypes.h"

namespace opaline
{
// Legacy envelope that advances in dB attenuation and converts to amplitude at output.
class OpalineEnvelope
{
public:
    enum class Stage
    {
        Off,
        Attack,
        Decay1,
        Decay2,
        Release
    };

    void reset(double sampleRate);
    void noteOn(const OpalineEnvelopeParams& params, int rateScale, int note);
    void noteOff();
    double next();
    bool isActive() const;
    Stage stage() const { return currentStage; }

private:
    void advanceEgTick(double targetD1Db);
    void advanceAttack();
    void advanceDecay(int rate, double targetDb, Stage nextStage, Stage rateStage);
    int egIncrement(int rate, Stage stage) const;
    double decay1LevelDb() const;

    OpalineEnvelopeParams currentParams;
    Stage currentStage = Stage::Off;
    double currentSampleRate = 44100.0;
    double attenuationDb = 96.0;
    int currentRateScale = 0;
    int currentNote = 60;
    int egCounter = 0;
    double egRemainder = 0.0;
};
} // namespace opaline
