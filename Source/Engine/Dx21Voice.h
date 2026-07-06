#pragma once

#include "Engine/Dx21ChipEnvelope.h"
#include "Engine/Dx21Envelope.h"
#include "Engine/Dx21PitchEnvelope.h"
#include "Engine/Dx21Types.h"

#include <array>
#include <cstdint>
#include <utility>

namespace dx21
{
struct Algorithm;

// 1オペレータの出力。audioは最終音声、modulationは次段へ送る位相変調量。
struct OperatorRender
{
    double audio = 0.0;
    double modulation = 0.0;
};

// 1音分のFMボイス。OLD/NEWの状態を持ち、renderModelで鳴らし分ける。
class Dx21Voice
{
public:
    void start(const Dx21Patch& patch, int midiNote, int velocity, double sampleRate);
    void release();
    bool isActive() const;
    int note() const { return midiNote; }
    double render(const Dx21Patch& patch,
                  double pitchBend,
                  double modWheel,
                  double globalLfoAge,
                  Dx21RenderModel renderModel);

private:
    OperatorRender renderOperator(int opIndex,
                                  const Dx21Patch& patch,
                                  const Algorithm& algorithm,
                                  double baseFrequency,
                                  double ampDepth,
                                  double lfoAm,
                                  Dx21RenderModel renderModel,
                                  std::array<bool, kOperatorCount>& computed,
                                  std::array<OperatorRender, kOperatorCount>& outputs);
    double nextOperatorLevel(int index, int targetLevel);
    double nextOperatorTl(int index, int targetLevel);
    double nextPitchModulation(double pitchLfo);
    std::pair<double, double> nextSampleAndHoldLfoShape(double phase);

    // 発音ごとの状態。位相、EG、フィードバック履歴はボイス単位で独立する。
    int midiNote = 60;
    int noteVelocity = 100;
    double currentSampleRate = 44100.0;
    double ageSeconds = 0.0;
    std::array<double, kOperatorCount> phases {};
    std::array<double, kOperatorCount> operatorOppTlUnits {};
    std::array<double, kOperatorCount> operatorTlAccumulators {};
    std::array<Dx21Envelope, kOperatorCount> envelopes {};
    std::array<Dx21ChipEnvelope, kOperatorCount> chipEnvelopes {};
    Dx21PitchEnvelope pitchEnvelope;
    Dx21RenderModel activeRenderModel = Dx21RenderModel::Current;
    double delayedPitchLfo = 0.0;
    std::uint32_t sampleAndHoldLfsr = 0;
    int sampleAndHoldBit = 0;
    int sampleAndHoldCycle = -1;
    int sampleAndHoldValue = 128;
    std::array<double, 2> feedbackHistory {};
    bool failed = false;
};
} // namespace dx21
