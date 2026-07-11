#pragma once

#include "Engine/OpalineTypes.h"

namespace opalineapp
{
enum class PerformanceMode
{
    Single = 0,
    Dual,
    Split
};

struct PerformanceState
{
    PerformanceMode mode = PerformanceMode::Single;
    bool monoA = false;
    bool monoB = false;
    int voiceAIndex = 0;
    int voiceBIndex = 16;
    int dualDetune = 0;
    int splitPoint = 60;
    int abBalance = 0;
};

struct SynthState
{
    opaline::OpalinePatch patch;
    PerformanceState performance;
    float masterVolume = 0.65f;
    int pitchBendRange = 2;
    int portamento = 0;
    opaline::OpalineRenderModel renderModel = opaline::OpalineRenderModel::TypeB;
};
} // namespace opalineapp
