#pragma once

#include <array>

namespace opaline
{
// carriers feed the final output; deps lists upstream modulators for each operator.
struct Algorithm
{
    std::array<int, 4> carriers {};
    int carrierCount = 0;
    std::array<std::array<int, 4>, 4> deps {};
    std::array<int, 4> depCounts {};
};

const std::array<double, 64>& opalineRatios();
const std::array<Algorithm, 8>& opalineAlgorithms();
double sineLookup(double phase);
double opalineLfoSpeedToHz(int speed);
double opmStyleDt1FrequencyOffset(double baseFrequency, double ratio, int detune, int note);
} // namespace opaline
