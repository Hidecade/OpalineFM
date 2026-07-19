#pragma once

#include "Engine/OpalineTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace opaline
{
// Fixed sizes for a compatible 32-voice bulk VMEM dump.
constexpr int kOpalineBulkVoiceCount = 32;
constexpr int kOpalineVmemVoiceSize = 128;
constexpr int kOpalineVcedVoiceSize = 93;
constexpr int kOpalineBulkVoiceDataOffset = 6;
constexpr int kOpalineBulkVoiceDataSize = kOpalineBulkVoiceCount * kOpalineVmemVoiceSize;
constexpr int kOpalineBulkMinimumSize = kOpalineBulkVoiceDataOffset + kOpalineBulkVoiceDataSize + 2;

struct OpalineVmemPreset
{
    std::string name;
    std::array<std::uint8_t, kOpalineVmemVoiceSize> vmem {};
};

struct OpalinePatchWithMetadata
{
    // patch is editable; vmem preserves the original SysEx representation.
    OpalinePatch patch;
    std::string name;
    bool effectsEnabled = true;
    bool hasVmem = false;
    std::array<std::uint8_t, kOpalineVmemVoiceSize> vmem {};
};

std::vector<OpalineVmemPreset> parseCompatibleBulkVmem(const std::vector<std::uint8_t>& bytes);
OpalinePatchWithMetadata decodeCompatibleVmemVoice(const std::array<std::uint8_t, kOpalineVmemVoiceSize>& vmem);
std::array<std::uint8_t, kOpalineVmemVoiceSize> encodeCompatibleVmemVoice(const OpalinePatchWithMetadata& voice);
std::vector<std::uint8_t> encodeCompatibleVcedVoice(const OpalinePatchWithMetadata& voice,
                                                    std::uint8_t midiChannel = 0);
std::vector<std::uint8_t> encodeCompatibleBulkVmem(const std::vector<OpalinePatchWithMetadata>& voices);
OpalinePatchWithMetadata withVmemPreset(const OpalinePatch& basePatch, const OpalineVmemPreset& preset);
void clearVmemPreset(OpalinePatchWithMetadata& patch);
} // namespace opaline
