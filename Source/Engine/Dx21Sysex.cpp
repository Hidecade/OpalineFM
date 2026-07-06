#include "Engine/Dx21Sysex.h"

#include <stdexcept>

namespace dx21
{
namespace
{
constexpr std::array<int, kOperatorCount> kDx21VmemOperatorOrder { 3, 1, 2, 0 };
constexpr std::array<std::uint8_t, kDx21BulkVoiceDataOffset> kDx21BulkHeader { 0xf0u, 0x43u, 0x00u, 0x04u, 0x20u, 0x00u };

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

void writeVoiceName(std::array<std::uint8_t, kDx21VmemVoiceSize>& vmem, const std::string& name)
{
    for (int i = 57; i <= 66; ++i)
        vmem[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(' ');

    for (std::size_t i = 0; i < 10 && i < name.size(); ++i)
    {
        const auto ch = static_cast<unsigned char>(name[i]);
        vmem[57 + i] = static_cast<std::uint8_t>((ch >= 0x20 && ch <= 0x7e) ? ch : ' ');
    }
}

void encodeOperatorBlock(std::array<std::uint8_t, kDx21VmemVoiceSize>& vmem,
                         const int base,
                         const Dx21Operator& op)
{
    vmem[static_cast<std::size_t>(base + 0)] = static_cast<std::uint8_t>(clampInt(op.envelope.attackRate, 0, 31));
    vmem[static_cast<std::size_t>(base + 1)] = static_cast<std::uint8_t>(clampInt(op.envelope.decay1Rate, 0, 31));
    vmem[static_cast<std::size_t>(base + 2)] = static_cast<std::uint8_t>(clampInt(op.envelope.decay2Rate, 0, 31));
    vmem[static_cast<std::size_t>(base + 3)] = static_cast<std::uint8_t>(clampInt(op.envelope.releaseRate, 0, 15));
    vmem[static_cast<std::size_t>(base + 4)] = static_cast<std::uint8_t>(clampInt(op.envelope.decay1Level, 0, 15));
    vmem[static_cast<std::size_t>(base + 5)] = static_cast<std::uint8_t>(clampInt(op.levelScale, 0, 99));
    vmem[static_cast<std::size_t>(base + 6)] = static_cast<std::uint8_t>((op.ampModEnable ? 0x40 : 0x00) | clampInt(op.velocity, 0, 7));
    vmem[static_cast<std::size_t>(base + 7)] = static_cast<std::uint8_t>(clampInt(op.level, 0, 99));
    vmem[static_cast<std::size_t>(base + 8)] = static_cast<std::uint8_t>(clampInt(op.ratioIndex, 0, 63));
    vmem[static_cast<std::size_t>(base + 9)] = static_cast<std::uint8_t>((clampInt(op.rateScale, 0, 3) << 3)
        | clampInt(op.detune + 3, 0, 6));
}

std::uint8_t yamahaChecksum(const std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::size_t size)
{
    int sum = 0;
    for (std::size_t i = 0; i < size; ++i)
        sum += bytes[offset + i] & 0x7f;

    return static_cast<std::uint8_t>((128 - (sum & 0x7f)) & 0x7f);
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
    patch.lfo.ampSensitivity = (packedLfo >> 2) & 0x03u;
    patch.lfo.pitchSensitivity = (packedLfo >> 4) & 0x07u;
    patch.transpose = clampInt(static_cast<int>(vmem[46]), 0, 48) - 24;
    patch.pitchEnvelope.rate1 = vmem[67];
    patch.pitchEnvelope.rate2 = vmem[68];
    patch.pitchEnvelope.rate3 = vmem[69];
    patch.pitchEnvelope.level1 = vmem[70];
    patch.pitchEnvelope.level2 = vmem[71];
    patch.pitchEnvelope.level3 = vmem[72];

    for (int block = 0; block < kOperatorCount; ++block)
    {
        const int opIndex = kDx21VmemOperatorOrder[static_cast<std::size_t>(block)];
        patch.operators[static_cast<std::size_t>(opIndex)] = decodeOperatorBlock(vmem, block * 10);
    }

    result.patch = normalizePatch(patch);
    return result;
}

std::array<std::uint8_t, kDx21VmemVoiceSize> encodeDx21VmemVoice(const Dx21PatchWithMetadata& voice)
{
    auto vmem = voice.hasVmem ? voice.vmem : std::array<std::uint8_t, kDx21VmemVoiceSize> {};
    const auto patch = normalizePatch(voice.patch);

    for (int block = 0; block < kOperatorCount; ++block)
    {
        const int opIndex = kDx21VmemOperatorOrder[static_cast<std::size_t>(block)];
        encodeOperatorBlock(vmem, block * 10, patch.operators[static_cast<std::size_t>(opIndex)]);
    }

    vmem[40] = static_cast<std::uint8_t>((clampInt(patch.algorithm, 1, 8) - 1)
        | (clampInt(patch.feedback, 0, 7) << 3)
        | (patch.lfo.sync ? 0x40 : 0x00));
    vmem[41] = static_cast<std::uint8_t>(clampInt(patch.lfo.speed, 0, 99));
    vmem[42] = static_cast<std::uint8_t>(clampInt(patch.lfo.delay, 0, 99));
    vmem[43] = static_cast<std::uint8_t>(clampInt(patch.lfo.pitchDepth, 0, 99));
    vmem[44] = static_cast<std::uint8_t>(clampInt(patch.lfo.ampDepth, 0, 99));
    vmem[45] = static_cast<std::uint8_t>(clampInt(patch.lfo.wave, 0, 3)
        | (clampInt(patch.lfo.ampSensitivity, 0, 3) << 2)
        | (clampInt(patch.lfo.pitchSensitivity, 0, 7) << 4));
    vmem[46] = static_cast<std::uint8_t>(clampInt(patch.transpose, -24, 24) + 24);
    vmem[67] = static_cast<std::uint8_t>(clampInt(patch.pitchEnvelope.rate1, 0, 99));
    vmem[68] = static_cast<std::uint8_t>(clampInt(patch.pitchEnvelope.rate2, 0, 99));
    vmem[69] = static_cast<std::uint8_t>(clampInt(patch.pitchEnvelope.rate3, 0, 99));
    vmem[70] = static_cast<std::uint8_t>(clampInt(patch.pitchEnvelope.level1, 0, 99));
    vmem[71] = static_cast<std::uint8_t>(clampInt(patch.pitchEnvelope.level2, 0, 99));
    vmem[72] = static_cast<std::uint8_t>(clampInt(patch.pitchEnvelope.level3, 0, 99));
    writeVoiceName(vmem, voice.name.empty() ? "INIT VOICE" : voice.name);
    return vmem;
}

std::vector<std::uint8_t> encodeDx21BulkVmem(const std::vector<Dx21PatchWithMetadata>& voices)
{
    if (voices.empty())
        throw std::runtime_error("Cannot encode an empty DX21 voice bank.");

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(kDx21BulkMinimumSize), 0);
    for (std::size_t i = 0; i < kDx21BulkHeader.size(); ++i)
        bytes[i] = kDx21BulkHeader[i];

    for (int voice = 0; voice < kDx21BulkVoiceCount; ++voice)
    {
        const auto& source = voices[static_cast<std::size_t>(voice) % voices.size()];
        const auto vmem = encodeDx21VmemVoice(source);
        const auto offset = static_cast<std::size_t>(kDx21BulkVoiceDataOffset + voice * kDx21VmemVoiceSize);
        for (int i = 0; i < kDx21VmemVoiceSize; ++i)
            bytes[offset + static_cast<std::size_t>(i)] = vmem[static_cast<std::size_t>(i)] & 0x7f;
    }

    bytes[static_cast<std::size_t>(kDx21BulkVoiceDataOffset + kDx21BulkVoiceDataSize)] =
        yamahaChecksum(bytes, kDx21BulkVoiceDataOffset, kDx21BulkVoiceDataSize);
    bytes.back() = 0xf7u;
    return bytes;
}

Dx21PatchWithMetadata withVmemPreset(const Dx21Patch& basePatch, const Dx21VmemPreset& preset)
{
    Dx21PatchWithMetadata patch = decodeDx21VmemVoice(preset.vmem);
    if (!preset.name.empty())
        patch.name = preset.name;

    return patch;
}

void clearVmemPreset(Dx21PatchWithMetadata& patch)
{
    patch.hasVmem = false;
    patch.vmem = {};
}
} // namespace dx21
