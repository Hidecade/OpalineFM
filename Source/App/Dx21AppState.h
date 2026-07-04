#pragma once

#include "Engine/Dx21Types.h"

namespace dx21app
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
};

struct SynthState
{
    dx21::Dx21Patch patch;
    PerformanceState performance;
    float masterVolume = 0.8f;
};
} // namespace dx21app
