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

enum class PortamentoMode
{
    Off = 0,
    Full,
    Finger
};

struct PerformanceState
{
    PerformanceMode mode = PerformanceMode::Single;
    bool monoA = false;
    bool monoB = false;
    PortamentoMode portamentoModeA = PortamentoMode::Off;
    PortamentoMode portamentoModeB = PortamentoMode::Off;
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
    int modWheelPitchRange = 99;
    int modWheelAmpRange = 0;
    bool effectsEnabled = true;
    opaline::OpalineRenderModel renderModel = opaline::OpalineRenderModel::TypeB;
};
} // namespace opalineapp
