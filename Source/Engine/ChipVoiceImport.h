#pragma once

#include "Engine/OpalineSysex.h"

#include <cstdint>
#include <string>
#include <vector>

namespace opaline
{
enum class ChipVoiceFormat
{
    Opm,
    Tfi,
    Vgi,
    Dmp
};

struct ChipVoiceImportResult
{
    ChipVoiceFormat format = ChipVoiceFormat::Tfi;
    std::vector<OpalinePatchWithMetadata> voices;
    std::vector<std::string> warnings;
};

bool isChipVoiceExtension(const std::string& extension);
ChipVoiceImportResult importChipVoices(const std::vector<std::uint8_t>& bytes,
                                       const std::string& extension,
                                       const std::string& fallbackName);
} // namespace opaline
