#include "Engine/OpalineVoiceLibrary.h"

#include <stdexcept>

namespace opaline
{
namespace
{
OpalinePatchWithMetadata makeInitVoice(const int index)
{
    OpalinePatchWithMetadata voice;
    voice.patch = normalizePatch(OpalinePatch {});
    voice.name = "INIT " + std::to_string(index + 1);
    voice.vmem = encodeCompatibleVmemVoice(voice);
    voice.hasVmem = true;
    return voice;
}

void validateBankIndex(const int bankIndex)
{
    if (bankIndex < 0 || bankIndex >= kOpalineVoiceBankCount)
        throw std::out_of_range("compatible voice bank index is out of range.");
}

void validateVoiceIndex(const int voiceIndex)
{
    if (voiceIndex < 0 || voiceIndex >= kOpalineVoiceBankSize)
        throw std::out_of_range("compatible voice index is out of range.");
}
} // namespace

OpalineVoiceBank makeInitVoiceBank(const std::string& name)
{
    OpalineVoiceBank bank;
    bank.name = name;
    for (int i = 0; i < kOpalineVoiceBankSize; ++i)
        bank.voices[static_cast<std::size_t>(i)] = makeInitVoice(i);

    return bank;
}

OpalineVoiceLibrary makeInitVoiceLibrary()
{
    OpalineVoiceLibrary library;
    for (int i = 0; i < kOpalineVoiceBankCount; ++i)
        library.banks[static_cast<std::size_t>(i)] = makeInitVoiceBank("Bank " + std::to_string(i + 1));

    return library;
}

OpalineVoiceBank voiceBankFromSysex(const std::vector<std::uint8_t>& bytes, const std::string& name)
{
    OpalineVoiceBank bank;
    bank.name = name;

    const auto presets = parseCompatibleBulkVmem(bytes);
    if (presets.size() != kOpalineVoiceBankSize)
        throw std::runtime_error("Expected exactly 32 compatible voices.");

    OpalinePatch basePatch;
    for (int i = 0; i < kOpalineVoiceBankSize; ++i)
        bank.voices[static_cast<std::size_t>(i)] = withVmemPreset(basePatch, presets[static_cast<std::size_t>(i)]);

    return bank;
}

std::vector<std::uint8_t> voiceBankToSysex(const OpalineVoiceBank& bank)
{
    std::vector<OpalinePatchWithMetadata> voices;
    voices.reserve(kOpalineVoiceBankSize);
    for (const auto& voice : bank.voices)
        voices.push_back(voice);

    return encodeCompatibleBulkVmem(voices);
}

const OpalinePatchWithMetadata& voiceAt(const OpalineVoiceLibrary& library, const int bankIndex, const int voiceIndex)
{
    validateBankIndex(bankIndex);
    validateVoiceIndex(voiceIndex);
    return library.banks[static_cast<std::size_t>(bankIndex)].voices[static_cast<std::size_t>(voiceIndex)];
}

OpalinePatchWithMetadata& voiceAt(OpalineVoiceLibrary& library, const int bankIndex, const int voiceIndex)
{
    validateBankIndex(bankIndex);
    validateVoiceIndex(voiceIndex);
    return library.banks[static_cast<std::size_t>(bankIndex)].voices[static_cast<std::size_t>(voiceIndex)];
}
} // namespace opaline
