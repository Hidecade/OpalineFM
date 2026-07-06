#pragma once

#include "Engine/Dx21Sysex.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dx21
{
constexpr int kDx21VoiceBankCount = 8;
constexpr int kDx21VoiceBankSize = kDx21BulkVoiceCount;

// UI/ホスト側で扱う音色バンク。DX21に合わせて1バンク32音色。
struct Dx21VoiceBank
{
    std::string name;
    std::array<Dx21PatchWithMetadata, kDx21VoiceBankSize> voices {};
};

struct Dx21VoiceLibrary
{
    std::array<Dx21VoiceBank, kDx21VoiceBankCount> banks {};
};

Dx21VoiceBank makeInitVoiceBank(const std::string& name = "Init Bank");
Dx21VoiceLibrary makeInitVoiceLibrary();
Dx21VoiceBank voiceBankFromSysex(const std::vector<std::uint8_t>& bytes, const std::string& name = "DX21.syx");
std::vector<std::uint8_t> voiceBankToSysex(const Dx21VoiceBank& bank);
const Dx21PatchWithMetadata& voiceAt(const Dx21VoiceLibrary& library, int bankIndex, int voiceIndex);
Dx21PatchWithMetadata& voiceAt(Dx21VoiceLibrary& library, int bankIndex, int voiceIndex);
} // namespace dx21
