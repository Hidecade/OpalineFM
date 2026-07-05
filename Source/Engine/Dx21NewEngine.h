#pragma once

#include "Engine/Dx21Types.h"
#include "Engine/Dx21Voice.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dx21
{
class Dx21NewEngine
{
public:
    void prepare(const double sampleRate, const int maxVoices = kDefaultMaxVoices)
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        maxVoiceCount = clampInt(maxVoices, 1, 32);
        chorusBuffer.assign(256, 0.0);
        voices.clear();
        voices.reserve(static_cast<std::size_t>(maxVoiceCount));
        panic();
    }

    void setPatch(const Dx21Patch& newPatch)
    {
        patch = normalizePatch(newPatch);
    }

    const Dx21Patch& getPatch() const
    {
        return patch;
    }

    void noteOn(const int note, const int velocity)
    {
        voices.erase(std::remove_if(voices.begin(),
                                    voices.end(),
                                    [note](const Dx21Voice& voice) { return voice.note() == note; }),
                     voices.end());

        Dx21Voice voice;
        voice.start(patch, clampInt(note, 0, 127), clampInt(velocity, 0, 127), currentSampleRate);
        voices.push_back(voice);

        while (static_cast<int>(voices.size()) > maxVoiceCount)
            voices.erase(voices.begin());

        if (patch.lfo.sync)
            globalLfoAge = 0.0;
    }

    void noteOff(const int note)
    {
        for (auto& voice : voices)
        {
            if (voice.note() == note)
                voice.release();
        }
    }

    void setPitchBend(const double value)
    {
        pitchBend = clampDouble(value, -1.0, 1.0);
    }

    void setModWheel(const double value)
    {
        modWheel = clampDouble(value, 0.0, 1.0);
    }

    void panic()
    {
        voices.clear();
        globalLfoAge = 0.0;
        lastLeft = 0.0;
        lastRight = 0.0;
        chorusWriteIndex = 0;
        chorusPhase = 0.0;
        std::fill(chorusBuffer.begin(), chorusBuffer.end(), 0.0);
    }

    void renderBlock(float* left, float* right, const int numSamples)
    {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto sample = renderSample();
            left[i] = sample.left;
            right[i] = sample.right;
        }
    }

    StereoSample renderSample()
    {
        globalLfoAge += 1.0 / currentSampleRate;
        double mixed = 0.0;

        for (auto& voice : voices)
            mixed += voice.renderWithFrequencyModel(patch,
                                                    pitchBend,
                                                    modWheel,
                                                    globalLfoAge,
                                                    Dx21FrequencyModel::Dx21Cents,
                                                    Dx21LevelModel::Ym2151Tl,
                                                    Dx21EnvelopeModel::Ym2151Opm);

        voices.erase(std::remove_if(voices.begin(),
                                    voices.end(),
                                    [](const Dx21Voice& voice) { return !voice.isActive(); }),
                     voices.end());

        return processYm2151StyleOutput(mixed);
    }

    int activeVoiceCount() const
    {
        return static_cast<int>(voices.size());
    }

private:
    StereoSample processYm2151StyleOutput(const double input)
    {
        constexpr double kYm2151OutputGain = 0.50;
        const double dry = std::tanh(input * kYm2151OutputGain);
        const double chorusAmount = static_cast<double>(patch.effects.chorus) / 99.0;

        double wet = 0.0;
        if (!chorusBuffer.empty())
        {
            chorusPhase += (0.34 + chorusAmount * 0.28) / currentSampleRate;
            if (chorusPhase >= 1.0)
                chorusPhase -= std::floor(chorusPhase);

            const double mod = 0.5 + 0.5 * std::sin(2.0 * kPi * chorusPhase);
            const double delaySamples = 18.0 + chorusAmount * (28.0 + mod * 46.0);
            double readPosition = static_cast<double>(chorusWriteIndex) - delaySamples;
            while (readPosition < 0.0)
                readPosition += static_cast<double>(chorusBuffer.size());

            const auto i0 = static_cast<std::size_t>(static_cast<int>(std::floor(readPosition)) % static_cast<int>(chorusBuffer.size()));
            const auto i1 = (i0 + 1) % chorusBuffer.size();
            const double fraction = readPosition - std::floor(readPosition);
            wet = chorusBuffer[i0] * (1.0 - fraction) + chorusBuffer[i1] * fraction;

            chorusBuffer[static_cast<std::size_t>(chorusWriteIndex)] = dry;
            chorusWriteIndex = (chorusWriteIndex + 1) % static_cast<int>(chorusBuffer.size());
        }

        const double wetGain = chorusAmount * 0.42;
        const double left = dry + wet * wetGain;
        const double right = dry - wet * wetGain;
        const double limitedLeft = std::tanh(left * 0.98);
        const double limitedRight = std::tanh(right * 0.98);
        lastLeft += clampDouble(limitedLeft - lastLeft, -0.35, 0.35);
        lastRight += clampDouble(limitedRight - lastRight, -0.35, 0.35);
        return { static_cast<float>(lastLeft), static_cast<float>(lastRight) };
    }

    Dx21Patch patch;
    std::vector<Dx21Voice> voices;
    std::vector<double> chorusBuffer;
    int chorusWriteIndex = 0;
    int maxVoiceCount = kDefaultMaxVoices;
    double currentSampleRate = 44100.0;
    double pitchBend = 0.0;
    double modWheel = 0.0;
    double globalLfoAge = 0.0;
    double chorusPhase = 0.0;
    double lastLeft = 0.0;
    double lastRight = 0.0;
};
} // namespace dx21
