#include "DSP/OpalineStereoDelay.h"

#include <algorithm>
#include <cmath>

namespace opaline
{
void OpalineStereoDelay::prepare(double newSampleRate)
{
    sampleRate = std::max(1.0, newSampleRate);
    const auto size = static_cast<std::size_t>(std::ceil(sampleRate * 2.1)) + 4U;
    leftBuffer.assign(size, 0.0);
    rightBuffer.assign(size, 0.0);
    reset();
}

void OpalineStereoDelay::reset()
{
    std::fill(leftBuffer.begin(), leftBuffer.end(), 0.0);
    std::fill(rightBuffer.begin(), rightBuffer.end(), 0.0);
    writeIndex = 0;
    lowPassLeft = lowPassRight = 0.0;
    echoPhase = 0.0;
}

double OpalineStereoDelay::readInterpolated(
    const std::vector<double>& buffer, double delaySamples) const
{
    if (buffer.empty()) return 0.0;
    auto position = static_cast<double>(writeIndex) - delaySamples;
    while (position < 0.0) position += static_cast<double>(buffer.size());
    const auto first = static_cast<std::size_t>(position) % buffer.size();
    const auto second = (first + 1U) % buffer.size();
    const auto fraction = position - std::floor(position);
    return buffer[first] + (buffer[second] - buffer[first]) * fraction;
}

StereoSample OpalineStereoDelay::process(
    StereoSample input, const StereoDelayParams& params)
{
    if (leftBuffer.empty() || params.mode == StereoDelayMode::off)
        return input;

    const auto target = std::clamp(params.timeSeconds, 0.01, 2.0) * sampleRate;
    const auto smoothing = 1.0 - std::exp(-1.0 / (0.02 * sampleRate));
    smoothedDelaySamples += (target - smoothedDelaySamples) * smoothing;
    const auto spread = std::clamp(params.spread, 0.0, 1.0) * 0.48;
    auto wetLeft = readInterpolated(leftBuffer,
                                    smoothedDelaySamples * (1.0 - spread));
    auto wetRight = readInterpolated(rightBuffer,
                                     smoothedDelaySamples * (1.0 + spread));

    const auto tone = std::clamp(params.tone, 0.0, 1.0);
    const auto cutoff = 700.0 * std::pow(22.0, tone);
    const auto coefficient = 1.0 - std::exp(-6.283185307179586 * cutoff / sampleRate);
    lowPassLeft += coefficient * (wetLeft - lowPassLeft);
    lowPassRight += coefficient * (wetRight - lowPassRight);
    wetLeft = lowPassLeft;
    wetRight = lowPassRight;

    auto feedbackLeft = wetLeft;
    auto feedbackRight = wetRight;
    if (params.mode == StereoDelayMode::pingPong)
        std::swap(feedbackLeft, feedbackRight);
    else if (params.mode == StereoDelayMode::echo)
    {
        echoPhase += 0.37 / sampleRate;
        if (echoPhase >= 1.0) echoPhase -= 1.0;
        const auto colour = 0.985
            + std::sin(echoPhase * 6.283185307179586) * 0.015;
        feedbackLeft *= colour;
        feedbackRight *= colour;
    }

    const auto feedback = std::clamp(params.feedback, 0.0, 0.94);
    leftBuffer[writeIndex] = std::tanh(
        static_cast<double>(input.left) + feedbackLeft * feedback);
    rightBuffer[writeIndex] = std::tanh(
        static_cast<double>(input.right) + feedbackRight * feedback);
    writeIndex = (writeIndex + 1U) % leftBuffer.size();

    if (params.mix <= 0.0)
        return input;
    const auto mix = std::clamp(params.mix, 0.0, 1.0);
    return {
        static_cast<float>(input.left * (1.0 - mix) + wetLeft * mix),
        static_cast<float>(input.right * (1.0 - mix) + wetRight * mix)
    };
}
} // namespace opaline
