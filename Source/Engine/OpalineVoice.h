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

namespace detail
{
// Generated once during module initialization; rendering only reads the tables.
const std::array<int, 256>& chipLogSinRom();
const std::array<int, 256>& chipExpRom();
}

// DX21-compatible per-operator keyboard level-scaling offset.
int keyboardLevelScaleOffset(int midiNote, int levelScale);

// Per-operator audio and phase-modulation bus outputs.
struct OperatorRender
{
    double audio = 0.0;
    double modulation = 0.0;
};

// One FM voice with independent phase, envelope, and feedback state.
class OpalineVoice
{
public:
    void start(const OpalinePatch& patch, int midiNote, int velocity, double sampleRate,
               OpalineRenderModel renderModel, int portamentoFromNote = -1,
               double portamentoSeconds = 0.0);
    void retargetPitch(int midiNote, double portamentoSeconds);
    void release();
    bool isActive() const;
    int note() const { return midiNote; }
    double render(const OpalinePatch& patch,
                  double pitchBend,
                  int pitchBendRange,
                  double modWheel,
                  int modWheelPitchRange,
                  int modWheelAmpRange,
                  double globalLfoAge,
                  OpalineRenderModel renderModel);

private:
    struct OperatorScaleCache
    {
        int midiNote = -1;
        int levelScale = -1;
        double tlOffset = 0.0;
    };

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

    // State that must remain independent for each active voice.
    int midiNote = 60;
    int noteVelocity = 100;
    double currentSampleRate = 44100.0;
    double ageSeconds = 0.0;
    double portamentoOffsetSemitones = 0.0;
    double portamentoStepPerSample = 0.0;
    double operatorTlStepInterval = 1.0;
    std::array<double, kOperatorCount> phases {};
    std::array<double, kOperatorCount> operatorOppTlUnits {};
    std::array<double, kOperatorCount> operatorTlAccumulators {};
    std::array<double, 8> carrierVelocityDb {};
    std::array<double, 8> modulatorVelocityDb {};
    std::array<OperatorScaleCache, kOperatorCount> operatorScaleCaches {};
    std::array<bool, kOperatorCount> operatorCarrierRoles {};
    std::array<OpalineEnvelope, kOperatorCount> envelopes {};
    std::array<OpalineChipEnvelope, kOperatorCount> chipEnvelopes {};
    OpalinePitchEnvelope pitchEnvelope;
    OpalineRenderModel activeRenderModel = OpalineRenderModel::TypeB;
    double delayedPitchLfo = 0.0;
    int cachedBaseMidiNote = -1000;
    double cachedBaseNoteFrequency = 0.0;
    int cachedLfoSpeed = -1;
    double cachedLfoFrequency = 0.0;
    int cachedLfoDelay = -1;
    double cachedLfoWaitSeconds = 0.0;
    double cachedLfoFadeSeconds = 0.0;
    int cachedAmpDepthValue = -1;
    int cachedAmpSensitivity = -1;
    double cachedAmpDepth = 0.0;
    int cachedCarrierCount = -1;
    double cachedChipCarrierGain = 1.0;
    double cachedCarrierDivisor = 1.0;
    int cachedAlgorithm = -1;
    int cachedFeedback = -1;
    double cachedFeedbackDivisor = 1.0;
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
