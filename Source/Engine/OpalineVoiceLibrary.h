#pragma once

#include "Engine/OpalineSysex.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace opaline
{
constexpr int kOpalineVoiceBankCount = 8;
constexpr int kOpalineVoiceBankSize = kOpalineBulkVoiceCount;

// UI/ホスト側で扱う音色バンク。compatibleに合わせて1バンク32音色。
struct OpalineVoiceBank
{
    std::string name;
    std::array<OpalinePatchWithMetadata, kOpalineVoiceBankSize> voices {};
};

struct OpalineVoiceLibrary
{
    std::array<OpalineVoiceBank, kOpalineVoiceBankCount> banks {};
};

OpalineVoiceBank makeInitVoiceBank(const std::string& name = "Init Bank");
OpalineVoiceLibrary makeInitVoiceLibrary();
OpalineVoiceBank voiceBankFromSysex(const std::vector<std::uint8_t>& bytes, const std::string& name = "factory.syx");
std::vector<std::uint8_t> voiceBankToSysex(const OpalineVoiceBank& bank);
const OpalinePatchWithMetadata& voiceAt(const OpalineVoiceLibrary& library, int bankIndex, int voiceIndex);
OpalinePatchWithMetadata& voiceAt(OpalineVoiceLibrary& library, int bankIndex, int voiceIndex);
} // namespace opaline
