#include "Engine/Dx21Sysex.h"

#include <stdexcept>

namespace dx21
{
namespace
{
constexpr std::array<int, kOperatorCount> kDx21VmemOperatorOrder { 3, 1, 2, 0 };

std::string readVoiceName(const std::array<std::uint8_t, kDx21VmemVoiceSize>& vmem)
{
    std::string name;
    for (int i = 57; i <= 66; ++i)
    {
        const auto ch = static_cast<char>(vmem[static_cast<std::size_t>(i)]);
        if (ch >= 0x20 && ch <= 0x7e)
            name.push_back(ch);
    }

    while (!name.empty() && name.back() == ' ')
        name.pop_back();

    return name;
}

Dx21Operator decodeOperatorBlock(const std::array<std::uint8_t, kDx21VmemVoiceSize>& vmem, const int base)
{
    Dx21Operator op;
    op.envelope.attackRate = vmem[static_cast<std::size_t>(base + 0)];
    op.envelope.decay1Rate = vmem[static_cast<std::size_t>(base + 1)];
    op.envelope.decay2Rate = vmem[static_cast<std::size_t>(base + 2)];
    op.envelope.releaseRate = vmem[static_cast<std::size_t>(base + 3)];
    op.envelope.decay1Level = vmem[static_cast<std::size_t>(base + 4)];
    op.levelScale = vmem[static_cast<std::size_t>(base + 5)];

    const auto packedVelocity = vmem[static_cast<std::size_t>(base + 6)];
    op.ampModEnable = (packedVelocity & 0x40u) != 0;
    op.velocity = packedVelocity & 0x07u;

    op.level = vmem[static_cast<std::size_t>(base + 7)];
    op.ratioIndex = vmem[static_cast<std::size_t>(base + 8)];

    const auto packedDetuneRateScale = vmem[static_cast<std::size_t>(base + 9)];
    op.detune = static_cast<int>(packedDetuneRateScale & 0x07u) - 3;
    op.rateScale = (packedDetuneRateScale >> 3) & 0x03u;
    return op;
}
} // namespace

std::vector<Dx21VmemPreset> parseDx21BulkVmem(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() < kDx21BulkMinimumSize || bytes.front() != 0xf0u || bytes.back() != 0xf7u)
        throw std::runtime_error("Expected a DX21 32-voice bulk SysEx file.");

    std::vector<Dx21VmemPreset> presets;
    presets.reserve(kDx21BulkVoiceCount);

    for (int voice = 0; voice < kDx21BulkVoiceCount; ++voice)
    {
        Dx21VmemPreset preset;
        const auto offset = static_cast<std::size_t>(kDx21BulkVoiceDataOffset + voice * kDx21VmemVoiceSize);
        for (int i = 0; i < kDx21VmemVoiceSize; ++i)
            preset.vmem[static_cast<std::size_t>(i)] = bytes[offset + static_cast<std::size_t>(i)];

        preset.name = readVoiceName(preset.vmem);
        presets.push_back(preset);
    }

    return presets;
}

Dx21PatchWithMetadata decodeDx21VmemVoice(const std::array<std::uint8_t, kDx21VmemVoiceSize>& vmem)
{
    Dx21PatchWithMetadata result;
    result.name = readVoiceName(vmem);
    result.hasVmem = true;
    result.vmem = vmem;
    auto& patch = result.patch;

    const auto packed = vmem[40];
    patch.algorithm = static_cast<int>(packed & 0x07u) + 1;
    patch.feedback = (packed >> 3) & 0x07u;
    patch.lfo.sync = (packed & 0x40u) != 0;

    patch.lfo.speed = vmem[41];
    patch.lfo.delay = vmem[42];
    patch.lfo.pitchDepth = vmem[43];
    patch.lfo.ampDepth = vmem[44];

    const auto packedLfo = vmem[45];
    patch.lfo.wave = packedLfo & 0x03u;
    patch.lfo.pitchSensitivity = (packedLfo >> 2) & 0x07u;
    patch.lfo.ampSensitivity = (packedLfo >> 5) & 0x03u;

    for (int block = 0; block < kOperatorCount; ++block)
    {
        const int opIndex = kDx21VmemOperatorOrder[static_cast<std::size_t>(block)];
        patch.operators[static_cast<std::size_t>(opIndex)] = decodeOperatorBlock(vmem, block * 10);
    }

    result.patch = normalizePatch(patch);
    return result;
}

Dx21PatchWithMetadata withVmemPreset(const Dx21Patch& basePatch, const Dx21VmemPreset& preset)
{
    Dx21PatchWithMetadata patch = decodeDx21VmemVoice(preset.vmem);
    if (!preset.name.empty())
        patch.name = preset.name;

    patch.patch.transpose = basePatch.transpose;
    return patch;
}

void clearVmemPreset(Dx21PatchWithMetadata& patch)
{
    patch.hasVmem = false;
    patch.vmem = {};
}
} // namespace dx21
