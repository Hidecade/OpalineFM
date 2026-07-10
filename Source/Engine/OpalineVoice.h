#pragma once

#include "Engine/OpalineChipEnvelope.h"
#include "Engine/OpalineEnvelope.h"
#include "Engine/OpalinePitchEnvelope.h"
#include "Engine/OpalineTypes.h"

#include <array>
#include <cstdint>
#include <utility>

namespace opaline
{
struct Algorithm;

// 1オペレータの出力。audioは最終音声、modulationは次段へ送る位相変調量。
struct OperatorRender
{
    double audio = 0.0;
    double modulation = 0.0;
};

// 1音分のFMボイス。OLD/NEWの状態を持ち、renderModelで鳴らし分ける。
class OpalineVoice
{
public:
    void start(const OpalinePatch& patch, int midiNote, int velocity, double sampleRate, OpalineRenderModel renderModel);
    void release();
    bool isActive() const;
    int note() const { return midiNote; }
    double render(const OpalinePatch& patch,
                  double pitchBend,
                  double modWheel,
                  double globalLfoAge,
                  OpalineRenderModel renderModel);

private:
    OperatorRender renderOperator(int opIndex,
                                  const OpalinePatch& patch,
                                  const Algorithm& algorithm,
                                  double baseFrequency,
                                  double ampDepth,
                                  double lfoAm,
                                  OpalineRenderModel renderModel,
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
    std::array<OpalineEnvelope, kOperatorCount> envelopes {};
    std::array<OpalineChipEnvelope, kOperatorCount> chipEnvelopes {};
    OpalinePitchEnvelope pitchEnvelope;
    OpalineRenderModel activeRenderModel = OpalineRenderModel::TypeB;
    double delayedPitchLfo = 0.0;
    std::uint32_t sampleAndHoldLfsr = 0;
    int sampleAndHoldBit = 0;
    int sampleAndHoldCycle = -1;
    int sampleAndHoldSubcycle = -1;
    int sampleAndHoldValue = 128;
    std::uint16_t sampleAndHoldShiftRegister = 0;
    std::array<double, 2> feedbackHistory {};
    bool failed = false;
};
} // namespace opaline
