#pragma once

#include <array>
#include <cstdint>

namespace opaline
{
constexpr int kOperatorCount = 4;
constexpr int kDefaultMaxVoices = 8;
constexpr double kPi = 3.14159265358979323846264338327950288;

// Core structures for compatible voice parameters used by the engine.
struct OpalineEnvelopeParams
{
    int attackRate = 20;
    int decay1Rate = 8;
    int decay1Level = 12;
    int decay2Rate = 1;
    int releaseRate = 6;
};

struct OpalineOperator
{
    int ratioIndex = 4;
    int detune = 0;
    int level = 70;
    int rateScale = 0;
    int levelScale = 0;
    int velocity = 2;
    bool ampModEnable = true;
    bool enabled = true;
    OpalineEnvelopeParams envelope;
};

struct OpalineLfo
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

struct OpalinePitchEnvelopeParams
{
    int rate1 = 99;
    int rate2 = 99;
    int rate3 = 99;
    int level1 = 50;
    int level2 = 50;
    int level3 = 50;
};

struct OpalineEffects
{
    int reverb = 0;
    int mix = 0;
    int echoMix = 0;
    int tone = 50;
    int chorus = 0;
    int delay = 0;
};

struct OpalinePatch
{
    int algorithm = 1;
    int feedback = 2;
    int transpose = 0;
    OpalineLfo lfo;
    OpalinePitchEnvelopeParams pitchEnvelope;
    OpalineEffects effects;
    std::array<OpalineOperator, kOperatorCount> operators {};
};

struct StereoSample
{
    float left = 0.0f;
    float right = 0.0f;
};

// TYPE A is the stable snapshot. TYPE B is the editable comparison model.
enum class OpalineRenderModel
{
    TypeA,
    TypeB
};

OpalinePatch normalizePatch(const OpalinePatch& patch);
double midiNoteToFrequency(double midiNote);
double clampDouble(double value, double low, double high);
int clampInt(int value, int low, int high);
} // namespace opaline
