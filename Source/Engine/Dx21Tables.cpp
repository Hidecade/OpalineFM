#include "Engine/Dx21Tables.h"

#include "Engine/Dx21Types.h"

#include <array>
#include <cmath>

namespace dx21
{
const std::array<double, 64>& dx21Ratios()
{
    static const std::array<double, 64> ratios {
        0.50, 0.71, 0.78, 0.87, 1.00, 1.41, 1.57, 1.73,
        2.00, 2.82, 3.00, 3.14, 3.46, 4.00, 4.24, 4.71,
        5.00, 5.19, 5.65, 6.00, 6.28, 6.92, 7.00, 7.07,
        7.85, 8.00, 8.48, 8.65, 9.00, 9.42, 9.89, 10.00,
        10.38, 10.99, 11.00, 11.30, 12.00, 12.11, 12.56, 12.72,
        13.00, 13.84, 14.00, 14.10, 14.13, 15.00, 15.55, 15.57,
        15.70, 16.96, 17.27, 17.30, 18.37, 18.84, 19.03, 19.78,
        20.41, 20.76, 21.20, 21.98, 22.49, 23.55, 24.22, 25.95
    };
    return ratios;
}

static Algorithm makeAlgorithm(const std::initializer_list<int> carriers,
                               const std::initializer_list<std::initializer_list<int>> deps)
{
    Algorithm algorithm;
    int index = 0;
    for (const int carrier : carriers)
        algorithm.carriers[static_cast<std::size_t>(index++)] = carrier;
    algorithm.carrierCount = index;

    int op = 0;
    for (const auto& depList : deps)
    {
        int depIndex = 0;
        for (const int dep : depList)
            algorithm.deps[static_cast<std::size_t>(op)][static_cast<std::size_t>(depIndex++)] = dep;
        algorithm.depCounts[static_cast<std::size_t>(op)] = depIndex;
        ++op;
    }

    return algorithm;
}

const std::array<Algorithm, 8>& dx21Algorithms()
{
    static const std::array<Algorithm, 8> algorithms {
        makeAlgorithm({ 0 }, { { 1 }, { 2 }, { 3 }, {} }),          // 4>3>2>1
        makeAlgorithm({ 0 }, { { 1 }, { 2, 3 }, {}, {} }),          // 4+3>2>1
        makeAlgorithm({ 0 }, { { 1, 3 }, { 2 }, {}, {} }),          // 3>2>1 + 4>1
        makeAlgorithm({ 0 }, { { 1, 2 }, {}, { 3 }, {} }),          // 2>1 + 4>3>1
        makeAlgorithm({ 0, 2 }, { { 1 }, {}, { 3 }, {} }),          // 2>1 + 4>3
        makeAlgorithm({ 0, 1, 2 }, { { 3 }, { 3 }, { 3 }, {} }),    // 4>(1+2+3)
        makeAlgorithm({ 0, 1, 2 }, { {}, {}, { 3 }, {} }),          // 4>3 + 1 + 2
        makeAlgorithm({ 0, 1, 2, 3 }, { {}, {}, {}, {} })           // 1 + 2 + 3 + 4
    };
    return algorithms;
}

double sineLookup(const double phase)
{
    return std::sin(phase);
}

double dx21LfoSpeedToHz(const int speed)
{
    const auto normalized = static_cast<double>(clampInt(speed, 0, 99)) / 99.0;
    if (normalized <= 0.0)
        return 0.01;

    return 55.0 * std::pow(normalized, 2.0246997291383155);
}

double opmStyleDt1FrequencyOffset(const double baseFrequency, const double ratio, const int detune, const int note)
{
    static constexpr std::array<int, 8> kOpmDt1Detune { 16, 17, 19, 20, 22, 24, 27, 29 };
    constexpr int kOpmBaseFnum = 1299;

    const int amount = clampInt(static_cast<int>(std::round(std::abs(detune))), 0, 3);
    if (amount <= 0)
        return 0.0;

    const double sign = detune < 0 ? -1.0 : 1.0;
    const int keyCode = std::min(clampInt(static_cast<int>(std::round((static_cast<double>(note) - 24.0) / 3.0)), 0, 31), 0x1c);
    const int block = keyCode >> 2;
    const int noteIndex = keyCode & 3;
    const int sum = block + 9 + ((amount == 3 || (amount & 2) != 0) ? 1 : 0);
    const int sumHigh = sum >> 1;
    const int sumLow = sum & 1;
    const int shift = 9 - sumHigh;
    const int raw = kOpmDt1Detune[static_cast<std::size_t>((sumLow << 2) | noteIndex)];
    const int detuneUnits = shift >= 0 ? (raw >> shift) : (raw << -shift);
    const int baseUnits = std::max(1, (kOpmBaseFnum << block) >> 2);
    return sign * baseFrequency * ratio * static_cast<double>(detuneUnits) / static_cast<double>(baseUnits);
}
} // namespace dx21
