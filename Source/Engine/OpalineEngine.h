#pragma once

#include "Engine/OpalineTypes.h"
#include "Engine/OpalineVoice.h"

#include <array>
#include <vector>

namespace opaline
{
// パッチ、発音中ボイス、グローバルLFO、簡易エフェクトを束ねる音源本体。
class OpalineEngine
{
public:
    void prepare(double sampleRate, int maxVoices = kDefaultMaxVoices);
    void setPatch(const OpalinePatch& newPatch);
    const OpalinePatch& getPatch() const { return patch; }

    void noteOn(int note, int velocity);
    void noteOff(int note);
    void setPitchBend(double value);
    void setPitchBendRange(int semitones);
    void setPortamento(int value);
    void setPortamentoMode(int mode);
    void setPortamentoFootSwitch(bool down);
    void setEffectsEnabled(bool enabled);
    void setMonoMode(bool enabled);
    void setSustainPedal(bool down);
    void setModWheel(double value);
    void setModWheelRanges(int pitchRange, int ampRange);
    void setRenderModel(OpalineRenderModel model) { renderModel = model; }
    void panic();

    void renderBlock(float* left, float* right, int numSamples);
    StereoSample renderSample();
    int activeVoiceCount() const { return static_cast<int>(voices.size()); }

private:
    double limitAndDeclick(double sample);
    StereoSample processEffects(double input);
    double readDelay(const std::vector<double>& buffer, int writeIndex, double delaySamples) const;
    void resetEffects();

    OpalinePatch patch;
    std::vector<OpalineVoice> voices;
    std::vector<double> delayBufferLeft;
    std::vector<double> delayBufferRight;
    std::vector<double> chorusBufferLeft;
    std::vector<double> chorusBufferRight;
    std::array<std::vector<double>, 4> reverbBufferLeft;
    std::array<std::vector<double>, 4> reverbBufferRight;
    std::array<int, 4> reverbWriteIndices {};
    int delayWriteIndex = 0;
    int chorusWriteIndex = 0;
    double currentSampleRate = 44100.0;
    int maxVoiceCount = kDefaultMaxVoices;
    double pitchBend = 0.0;
    int pitchBendRange = 2;
    int portamento = 0;
    int portamentoMode = 0;
    bool portamentoFootSwitchDown = true;
    bool effectsEnabled = true;
    bool monoMode = false;
    bool sustainPedalDown = false;
    std::array<bool, 128> sustainedNotes {};
    std::array<bool, 128> keyDownNotes {};
    int lastPlayedNote = -1;
    double modWheel = 0.0;
    int modWheelPitchRange = 99;
    int modWheelAmpRange = 0;
    OpalineRenderModel renderModel = OpalineRenderModel::TypeB;
    double globalLfoAge = 0.0;
    double chorusPhase = 0.0;
    double toneLeft = 0.0;
    double toneRight = 0.0;
    double lastOutput = 0.0;
    double lastLeft = 0.0;
    double lastRight = 0.0;
};
} // namespace opaline
