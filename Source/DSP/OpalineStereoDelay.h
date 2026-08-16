#pragma once

#include "Engine/OpalineTypes.h"

#include <vector>

namespace opaline
{
enum class StereoDelayMode { off = 0, stereo, pingPong, echo };

struct StereoDelayParams
{
    StereoDelayMode mode = StereoDelayMode::off;
    double timeSeconds = 0.25;
    double spread = 0.0;
    double feedback = 0.25;
    double tone = 0.75;
    double mix = 0.0;
};

// The same interpolated, tone-filtered and saturated delay topology used by
// Aureline, expressed with Opaline FM's StereoSample type.
class OpalineStereoDelay
{
public:
    void prepare(double newSampleRate);
    void reset();
    StereoSample process(StereoSample input, const StereoDelayParams& params);

private:
    double readInterpolated(const std::vector<double>& buffer,
                            double delaySamples) const;

    std::vector<double> leftBuffer, rightBuffer;
    double sampleRate = 44100.0;
    std::size_t writeIndex = 0;
    double smoothedDelaySamples = 11025.0;
    double lowPassLeft = 0.0, lowPassRight = 0.0;
    double echoPhase = 0.0;
};
} // namespace opaline
