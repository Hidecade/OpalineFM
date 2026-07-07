#pragma once

#include <array>

namespace opaline
{
// carriersは最終出力、depsは各OPへ入る上流モジュレータを表す。
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
