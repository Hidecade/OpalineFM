#pragma once

#include <array>
#include <cstdint>

namespace dx21
{
constexpr int kOperatorCount = 4;
constexpr int kDefaultMaxVoices = 8;
constexpr double kPi = 3.14159265358979323846264338327950288;

struct Dx21EnvelopeParams
{
    int attackRate = 20;
    int decay1Rate = 8;
    int decay1Level = 12;
    int decay2Rate = 1;
    int releaseRate = 6;
};

struct Dx21Operator
{
    int ratioIndex = 4;
    int detune = 0;
    int level = 70;
    int rateScale = 0;
    int levelScale = 0;
    int velocity = 2;
    bool ampModEnable = true;
    bool enabled = true;
    Dx21EnvelopeParams envelope;
};

struct Dx21Lfo
{
    int speed = 24;
    int delay = 0;
    int pitchDepth = 0;
    int ampDepth = 0;
    int pitchSensitivity = 3;
    int ampSensitivity = 0;
    bool sync = false;
    int wave = 0;
};

struct Dx21PitchEnvelopeParams
{
    int rate1 = 99;
    int rate2 = 99;
    int rate3 = 99;
    int level1 = 50;
    int level2 = 50;
    int level3 = 50;
};

struct Dx21Effects
{
    int reverb = 0;
    int mix = 0;
    int tone = 50;
    int chorus = 0;
    int delay = 0;
};

struct Dx21Patch
{
    int algorithm = 1;
    int feedback = 2;
    int transpose = 0;
    Dx21Lfo lfo;
    Dx21PitchEnvelopeParams pitchEnvelope;
    Dx21Effects effects;
    std::array<Dx21Operator, kOperatorCount> operators {};
};

struct StereoSample
{
    float left = 0.0f;
    float right = 0.0f;
};

Dx21Patch normalizePatch(const Dx21Patch& patch);
double midiNoteToFrequency(double midiNote);
double clampDouble(double value, double low, double high);
int clampInt(int value, int low, int high);
} // namespace dx21
