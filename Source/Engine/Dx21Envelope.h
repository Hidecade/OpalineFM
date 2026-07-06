#pragma once

#include "Engine/Dx21Types.h"

namespace dx21
{
// 従来モデル用EG。内部はdB減衰量として進め、最後に振幅へ変換する。
class Dx21Envelope
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
    Stage stage() const { return currentStage; }

private:
    void advanceEgTick(double targetD1Db);
    void advanceAttack();
    void advanceDecay(int rate, double targetDb, Stage nextStage, Stage rateStage);
    int egIncrement(int rate, Stage stage) const;
    double decay1LevelDb() const;

    Dx21EnvelopeParams currentParams;
    Stage currentStage = Stage::Off;
    double currentSampleRate = 44100.0;
    double attenuationDb = 96.0;
    int currentRateScale = 0;
    int currentNote = 60;
    int egCounter = 0;
    double egRemainder = 0.0;
};
} // namespace dx21
