#include "Engine/Dx21Engine.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
template <typename T>
void writeLe(std::ofstream& out, T value)
{
    for (std::size_t i = 0; i < sizeof(T); ++i)
        out.put(static_cast<char>((static_cast<std::uint64_t>(value) >> (i * 8)) & 0xffu));
}

void writeWav(const std::string& path, const std::vector<float>& left, const std::vector<float>& right, int sampleRate)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
        throw std::runtime_error("Failed to open output WAV: " + path);

    const std::uint16_t channels = 2;
    const std::uint16_t bitsPerSample = 16;
    const std::uint32_t frameCount = static_cast<std::uint32_t>(std::min(left.size(), right.size()));
    const std::uint32_t dataBytes = frameCount * channels * bitsPerSample / 8;

    out.write("RIFF", 4);
    writeLe<std::uint32_t>(out, 36u + dataBytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeLe<std::uint32_t>(out, 16);
    writeLe<std::uint16_t>(out, 1);
    writeLe<std::uint16_t>(out, channels);
    writeLe<std::uint32_t>(out, static_cast<std::uint32_t>(sampleRate));
    writeLe<std::uint32_t>(out, static_cast<std::uint32_t>(sampleRate * channels * bitsPerSample / 8));
    writeLe<std::uint16_t>(out, static_cast<std::uint16_t>(channels * bitsPerSample / 8));
    writeLe<std::uint16_t>(out, bitsPerSample);
    out.write("data", 4);
    writeLe<std::uint32_t>(out, dataBytes);

    for (std::uint32_t i = 0; i < frameCount; ++i)
    {
        const auto l = static_cast<std::int16_t>(std::clamp(left[i], -1.0f, 1.0f) * 32767.0f);
        const auto r = static_cast<std::int16_t>(std::clamp(right[i], -1.0f, 1.0f) * 32767.0f);
        writeLe<std::uint16_t>(out, static_cast<std::uint16_t>(l));
        writeLe<std::uint16_t>(out, static_cast<std::uint16_t>(r));
    }
}

int parseIntArg(char** argv, int argc, int index, int fallback)
{
    if (index >= argc)
        return fallback;
    return std::stoi(argv[index]);
}

double parseDoubleArg(char** argv, int argc, int index, double fallback)
{
    if (index >= argc)
        return fallback;
    return std::stod(argv[index]);
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::string output = argc > 1 ? argv[1] : "dx21_render.wav";
        const int note = parseIntArg(argv, argc, 2, 60);
        const int velocity = parseIntArg(argv, argc, 3, 104);
        const double seconds = parseDoubleArg(argv, argc, 4, 3.0);
        const int sampleRate = parseIntArg(argv, argc, 5, 44100);

        dx21::Dx21Patch patch;
        patch.algorithm = 5;
        patch.feedback = 3;
        patch.lfo.speed = 32;
        patch.lfo.pitchDepth = 8;
        patch.lfo.pitchSensitivity = 3;
        patch.operators[0].level = 90;
        patch.operators[1].level = 68;
        patch.operators[2].level = 75;
        patch.operators[3].level = 58;
        patch.operators[0].ratioIndex = 4;
        patch.operators[1].ratioIndex = 8;
        patch.operators[2].ratioIndex = 4;
        patch.operators[3].ratioIndex = 13;

        dx21::Dx21Engine engine;
        engine.prepare(static_cast<double>(sampleRate));
        engine.setPatch(patch);
        engine.noteOn(note, velocity);

        const int totalSamples = static_cast<int>(seconds * static_cast<double>(sampleRate));
        const int noteOffSample = static_cast<int>(std::max(0.1, seconds * 0.72) * static_cast<double>(sampleRate));
        std::vector<float> left(static_cast<std::size_t>(totalSamples));
        std::vector<float> right(static_cast<std::size_t>(totalSamples));

        for (int i = 0; i < totalSamples; ++i)
        {
            if (i == noteOffSample)
                engine.noteOff(note);

            const auto sample = engine.renderSample();
            left[static_cast<std::size_t>(i)] = sample.left;
            right[static_cast<std::size_t>(i)] = sample.right;
        }

        writeWav(output, left, right, sampleRate);
        std::cout << "Wrote " << output << " note=" << note << " velocity=" << velocity
                  << " seconds=" << seconds << " sampleRate=" << sampleRate << '\n';
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
