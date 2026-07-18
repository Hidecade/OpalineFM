#pragma once

#include "Engine/OpalineTypes.h"
#include "Engine/OpalineVoice.h"

#include <array>
#include <vector>

namespace opaline
{
// Shared synth engine for patches, voices, global LFO, and effects.
class OpalineEngine
{
public:
    void prepare(double sampleRate, int maxVoices = kDefaultMaxVoices);
    void setVoiceLimit(int maxVoices);
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
    void updateEffectParameters();

    OpalinePatch patch;
    std::vector<OpalineVoice> voices;
    std::vector<double> delayBufferLeft;
    std::vector<double> delayBufferRight;
    std::vector<double> chorusBufferLeft;
    std::vector<double> chorusBufferRight;
    std::array<std::vector<double>, 4> reverbBufferLeft;
    std::array<std::vector<double>, 4> reverbBufferRight;
    std::array<double, 4> reverbDampingLeft {};
    std::array<double, 4> reverbDampingRight {};
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
    double effectReverb = 0.0;
    double effectReverbMix = 0.0;
    double effectEchoMix = 0.0;
    double effectTone = 0.0;
    double effectChorus = 0.0;
    double effectDelay = 0.0;
    double effectDryGain = 1.0;
    double effectReverbWetGain = 0.0;
    double effectEchoWetGain = 0.0;
    double effectReverbFeedback = 0.48;
    double effectReverbDamping = 0.08;
    double effectDelaySamples = 0.0;
    double effectDelayFeedback = 0.0;
    double effectToneCoeff = 0.0;
    double effectChorusPhaseIncrement = 0.0;
    double effectChorusDelay = 0.0;
    double effectChorusDepth = 0.0;
};
} // namespace opaline
