#include "Engine/OpalineSysex.h"
#include "Engine/OpalineTables.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
struct VelocityProfile
{
    int carrier;
    int modulator;
};

constexpr std::array<VelocityProfile, opaline::kOpalineBulkVoiceCount> kVelocityProfiles {{
    { 5, 3 }, { 5, 3 }, { 5, 4 }, { 5, 3 }, { 5, 4 }, { 3, 2 }, { 2, 1 }, { 3, 2 },
    { 6, 4 }, { 5, 3 }, { 4, 2 }, { 5, 3 }, { 5, 3 }, { 4, 2 }, { 5, 3 }, { 5, 4 },
    { 5, 3 }, { 5, 3 }, { 6, 4 }, { 6, 4 }, { 5, 3 }, { 6, 4 }, { 5, 3 }, { 3, 2 },
    { 5, 3 }, { 5, 3 }, { 5, 4 }, { 3, 2 }, { 6, 3 }, { 6, 4 }, { 5, 4 }, { 6, 4 },
}};

std::vector<std::uint8_t> readFile(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Could not open " + path);

    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

bool isCarrier(const opaline::Algorithm& algorithm, const int opIndex)
{
    for (int i = 0; i < algorithm.carrierCount; ++i)
    {
        if (algorithm.carriers[static_cast<std::size_t>(i)] == opIndex)
            return true;
    }

    return false;
}

int selectTimbreOperator(const opaline::OpalinePatch& patch, const opaline::Algorithm& algorithm)
{
    int selected = -1;
    int highestLevel = -1;
    for (int opIndex = 0; opIndex < opaline::kOperatorCount; ++opIndex)
    {
        const auto& op = patch.operators[static_cast<std::size_t>(opIndex)];
        if (!isCarrier(algorithm, opIndex) && op.level > highestLevel)
        {
            selected = opIndex;
            highestLevel = op.level;
        }
    }

    if (selected >= 0 && highestLevel > 0)
        return selected;

    for (int opIndex = 0; opIndex < opaline::kOperatorCount; ++opIndex)
    {
        if (patch.operators[static_cast<std::size_t>(opIndex)].level > highestLevel)
        {
            selected = opIndex;
            highestLevel = patch.operators[static_cast<std::size_t>(opIndex)].level;
        }
    }
    return selected;
}

void tuneVoice(opaline::OpalinePatchWithMetadata& voice, const std::size_t voiceIndex)
{
    auto& patch = voice.patch;
    const auto& algorithm = opaline::opalineAlgorithms()[static_cast<std::size_t>(patch.algorithm - 1)];
    const auto profile = kVelocityProfiles[voiceIndex];
    bool alreadyTuned = true;

    for (int opIndex = 0; opIndex < opaline::kOperatorCount; ++opIndex)
    {
        const auto& op = patch.operators[static_cast<std::size_t>(opIndex)];
        if (op.level <= 0)
            continue;

        const int target = isCarrier(algorithm, opIndex) ? profile.carrier : profile.modulator;
        alreadyTuned = alreadyTuned && op.velocity >= target;
    }
    if (alreadyTuned)
        return;

    for (int opIndex = 0; opIndex < opaline::kOperatorCount; ++opIndex)
    {
        auto& op = patch.operators[static_cast<std::size_t>(opIndex)];
        if (op.level <= 0)
            continue;

        const int target = isCarrier(algorithm, opIndex) ? profile.carrier : profile.modulator;
        op.velocity = std::max(op.velocity, target);
    }

    // Give every factory voice a small independent timbral variation without
    // changing its algorithm, ratios, or overall envelope character.
    patch.feedback = patch.feedback >= 7 ? 6 : std::min(7, patch.feedback + 1);

    const int timbreOperator = selectTimbreOperator(patch, algorithm);
    if (timbreOperator >= 0)
    {
        auto& op = patch.operators[static_cast<std::size_t>(timbreOperator)];
        const int detuneDirection = (voiceIndex % 2 == 0) ? 1 : -1;
        op.detune = std::clamp(op.detune + detuneDirection, -3, 3);

        const int rateDirection = (voiceIndex % 3 == 0) ? 1 : -1;
        op.envelope.decay2Rate = std::clamp(op.envelope.decay2Rate + rateDirection, 0, 31);
    }

    if (patch.lfo.pitchDepth > 0 || patch.lfo.ampDepth > 0)
    {
        const int speedDelta = (voiceIndex % 2 == 0) ? 2 : -2;
        patch.lfo.speed = std::clamp(patch.lfo.speed + speedDelta, 0, 99);
    }
}

void writeFile(const std::string& path, const std::vector<std::uint8_t>& bytes)
{
    const std::filesystem::path destination(path);
    const auto temporary = destination.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            throw std::runtime_error("Could not create " + temporary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output)
            throw std::runtime_error("Could not write " + temporary);
    }
    std::filesystem::rename(temporary, destination);
}

void printBank(const std::vector<opaline::OpalinePatchWithMetadata>& voices)
{
    std::cout << " #  Name        Alg FB | OP1       OP2       OP3       OP4\n";
    std::cout << "                       | role L V  role L V  role L V  role L V\n";
    for (std::size_t voiceIndex = 0; voiceIndex < voices.size(); ++voiceIndex)
    {
        const auto& voice = voices[voiceIndex];
        const auto& patch = voice.patch;
        const auto& algorithm = opaline::opalineAlgorithms()[static_cast<std::size_t>(patch.algorithm - 1)];

        std::cout << std::setw(2) << voiceIndex + 1 << "  "
                  << std::left << std::setw(10) << voice.name << std::right
                  << "  " << patch.algorithm << "  " << patch.feedback << " |";

        for (int opIndex = 0; opIndex < opaline::kOperatorCount; ++opIndex)
        {
            const auto& op = patch.operators[static_cast<std::size_t>(opIndex)];
            std::cout << " " << (isCarrier(algorithm, opIndex) ? 'C' : 'M')
                      << " " << std::setw(2) << op.level
                      << " " << op.velocity << "  ";
        }
        std::cout << '\n';
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::string path = argc >= 2 ? argv[1] : "assets/factory.syx";
        const bool writeChanges = argc >= 3 && std::string(argv[2]) == "--write";
        const auto presets = opaline::parseCompatibleBulkVmem(readFile(path));

        std::vector<opaline::OpalinePatchWithMetadata> voices;
        voices.reserve(presets.size());
        for (const auto& preset : presets)
            voices.push_back(opaline::decodeCompatibleVmemVoice(preset.vmem));

        if (writeChanges)
        {
            for (std::size_t voiceIndex = 0; voiceIndex < voices.size(); ++voiceIndex)
                tuneVoice(voices[voiceIndex], voiceIndex);
            writeFile(path, opaline::encodeCompatibleBulkVmem(voices));
            std::cout << "Updated " << path << "\n\n";
        }

        printBank(voices);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
