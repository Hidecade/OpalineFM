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
    opaline::OpalineRenderModel renderModel = opaline::OpalineRenderModel::Current;
};
} // namespace opalineapp
