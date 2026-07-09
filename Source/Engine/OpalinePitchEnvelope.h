#pragma once

#include "Engine/OpalineTypes.h"

namespace opaline
{
// compatibleのPitch EG。音量EGとは別に、音程をセント単位で段階遷移させる。
class OpalinePitchEnvelope
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
    void noteOn(const OpalinePitchEnvelopeParams& params);
    void noteOff();
    double nextSemitones();
    Stage stage() const { return currentStage; }

private:
    void startSegment(Stage stage, int targetLevel, int rate);
    void advanceSegment();
    void completeSegment();
    static double levelToCents(int level);
    static double rateToBaseTimeSeconds(int rate);

    OpalinePitchEnvelopeParams currentParams;
    Stage currentStage = Stage::Off;
    double currentSampleRate = 44100.0;
    double currentCents = 0.0;
    double targetCents = 0.0;
    double centsPerSample = 0.0;
    int jumpSamplesRemaining = 0;
};
} // namespace opaline
