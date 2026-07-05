#pragma once

#include "Engine/Dx21Types.h"

namespace dx21
{
class Dx21PitchEnvelope
{
public:
    enum class Stage
    {
        Off,
        Stage1,
        Stage2,
        Sustain,
        Release,
        Finished
    };

    void reset(double sampleRate);
    void noteOn(const Dx21PitchEnvelopeParams& params);
    void noteOff();
    double nextSemitones();
    Stage stage() const { return currentStage; }

private:
    void startSegment(Stage stage, int targetLevel, int rate);
    void advanceSegment();
    static double levelToCents(int level);
    static double rateToBaseTimeSeconds(int rate);

    Dx21PitchEnvelopeParams currentParams;
    Stage currentStage = Stage::Off;
    double currentSampleRate = 44100.0;
    double currentCents = 0.0;
    double targetCents = 0.0;
    double centsPerSample = 0.0;
};
} // namespace dx21
