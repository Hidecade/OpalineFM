#pragma once

#include "Engine/Dx21Types.h"

namespace dx21
{
class Dx21ChipEnvelope
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
    void noteOn(const Dx21EnvelopeParams& params, int rateScale, int note);
    void noteOff();
    double next();
    bool isActive() const;

private:
    int egIncrement(int rate, Stage stage) const;
    double decay1TargetIndex() const;
    void advanceEgTick();
    void advanceAttack();
    void advanceDecay(int rate, double targetIndex, Stage nextStage, Stage rateStage);

    Dx21EnvelopeParams currentParams;
    Stage currentStage = Stage::Off;
    double currentSampleRate = 44100.0;
    double egLevel = 1023.0;
    int currentRateScale = 0;
    int currentNote = 60;
    int egCounter = 0;
    double egRemainder = 0.0;
};
} // namespace dx21
