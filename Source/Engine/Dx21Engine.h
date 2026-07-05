#pragma once

#include "Engine/Dx21Types.h"
#include "Engine/Dx21Voice.h"

#include <array>
#include <vector>

namespace dx21
{
class Dx21Engine
{
public:
    void prepare(double sampleRate, int maxVoices = kDefaultMaxVoices);
    void setPatch(const Dx21Patch& newPatch);
    const Dx21Patch& getPatch() const { return patch; }

    void noteOn(int note, int velocity);
    void noteOff(int note);
    void setPitchBend(double value);
    void setModWheel(double value);
    void setRenderModel(Dx21RenderModel model) { renderModel = model; }
    void panic();

    void renderBlock(float* left, float* right, int numSamples);
    StereoSample renderSample();
    int activeVoiceCount() const { return static_cast<int>(voices.size()); }

private:
    double limitAndDeclick(double sample);
    StereoSample processEffects(double input);
    double readDelay(const std::vector<double>& buffer, int writeIndex, double delaySamples) const;
    void resetEffects();

    Dx21Patch patch;
    std::vector<Dx21Voice> voices;
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
    double modWheel = 0.0;
    Dx21RenderModel renderModel = Dx21RenderModel::Current;
    double globalLfoAge = 0.0;
    double chorusPhase = 0.0;
    double toneLeft = 0.0;
    double toneRight = 0.0;
    double lastOutput = 0.0;
    double lastLeft = 0.0;
    double lastRight = 0.0;
};
} // namespace dx21
