#pragma once

#include "Engine/OpalineTypes.h"

namespace opaline
{
// Chip-style envelope that advances a 10-bit EG index directly to retain stepped behavior.
class OpalineChipEnvelope
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
    double nextIndex();
    bool isActive() const;

private:
    int egIncrement(int rate, Stage stage) const;
    double decay1TargetIndex() const;
    void advanceEgTick();
    void advanceAttack();
    void advanceDecay(int rate, double targetIndex, Stage nextStage, Stage rateStage);

    OpalineEnvelopeParams currentParams;
    Stage currentStage = Stage::Off;
    double currentSampleRate = 44100.0;
    double egLevel = 1023.0;
    int currentRateScale = 0;
    int currentNote = 60;
    int egCounter = 0;
    double egRemainder = 0.0;
};
} // namespace opaline
