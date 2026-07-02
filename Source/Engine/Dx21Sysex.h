#pragma once

#include "Engine/Dx21Types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dx21
{
constexpr int kDx21BulkVoiceCount = 32;
constexpr int kDx21VmemVoiceSize = 128;
constexpr int kDx21BulkVoiceDataOffset = 6;
constexpr int kDx21BulkVoiceDataSize = kDx21BulkVoiceCount * kDx21VmemVoiceSize;
constexpr int kDx21BulkMinimumSize = kDx21BulkVoiceDataOffset + kDx21BulkVoiceDataSize + 2;

struct Dx21VmemPreset
{
    std::string name;
    std::array<std::uint8_t, kDx21VmemVoiceSize> vmem {};
};

struct Dx21PatchWithMetadata
{
    Dx21Patch patch;
    std::string name;
    bool hasVmem = false;
    std::array<std::uint8_t, kDx21VmemVoiceSize> vmem {};
};

std::vector<Dx21VmemPreset> parseDx21BulkVmem(const std::vector<std::uint8_t>& bytes);
Dx21PatchWithMetadata decodeDx21VmemVoice(const std::array<std::uint8_t, kDx21VmemVoiceSize>& vmem);
Dx21PatchWithMetadata withVmemPreset(const Dx21Patch& basePatch, const Dx21VmemPreset& preset);
void clearVmemPreset(Dx21PatchWithMetadata& patch);
} // namespace dx21
