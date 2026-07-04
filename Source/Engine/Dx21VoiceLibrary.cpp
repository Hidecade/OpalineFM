#include "Engine/Dx21VoiceLibrary.h"

#include <stdexcept>

namespace dx21
{
namespace
{
Dx21PatchWithMetadata makeInitVoice(const int index)
{
    Dx21PatchWithMetadata voice;
    voice.patch = normalizePatch(Dx21Patch {});
    voice.name = "INIT " + std::to_string(index + 1);
    voice.vmem = encodeDx21VmemVoice(voice);
    voice.hasVmem = true;
    return voice;
}

void validateBankIndex(const int bankIndex)
{
    if (bankIndex < 0 || bankIndex >= kDx21VoiceBankCount)
        throw std::out_of_range("DX21 voice bank index is out of range.");
}

void validateVoiceIndex(const int voiceIndex)
{
    if (voiceIndex < 0 || voiceIndex >= kDx21VoiceBankSize)
        throw std::out_of_range("DX21 voice index is out of range.");
}
} // namespace

Dx21VoiceBank makeInitVoiceBank(const std::string& name)
{
    Dx21VoiceBank bank;
    bank.name = name;
    for (int i = 0; i < kDx21VoiceBankSize; ++i)
        bank.voices[static_cast<std::size_t>(i)] = makeInitVoice(i);

    return bank;
}

Dx21VoiceLibrary makeInitVoiceLibrary()
{
    Dx21VoiceLibrary library;
    for (int i = 0; i < kDx21VoiceBankCount; ++i)
        library.banks[static_cast<std::size_t>(i)] = makeInitVoiceBank("Bank " + std::to_string(i + 1));

    return library;
}

Dx21VoiceBank voiceBankFromSysex(const std::vector<std::uint8_t>& bytes, const std::string& name)
{
    Dx21VoiceBank bank;
    bank.name = name;

    const auto presets = parseDx21BulkVmem(bytes);
    if (presets.size() != kDx21VoiceBankSize)
        throw std::runtime_error("Expected exactly 32 DX21 voices.");

    Dx21Patch basePatch;
    for (int i = 0; i < kDx21VoiceBankSize; ++i)
        bank.voices[static_cast<std::size_t>(i)] = withVmemPreset(basePatch, presets[static_cast<std::size_t>(i)]);

    return bank;
}

std::vector<std::uint8_t> voiceBankToSysex(const Dx21VoiceBank& bank)
{
    std::vector<Dx21PatchWithMetadata> voices;
    voices.reserve(kDx21VoiceBankSize);
    for (const auto& voice : bank.voices)
        voices.push_back(voice);

    return encodeDx21BulkVmem(voices);
}

const Dx21PatchWithMetadata& voiceAt(const Dx21VoiceLibrary& library, const int bankIndex, const int voiceIndex)
{
    validateBankIndex(bankIndex);
    validateVoiceIndex(voiceIndex);
    return library.banks[static_cast<std::size_t>(bankIndex)].voices[static_cast<std::size_t>(voiceIndex)];
}

Dx21PatchWithMetadata& voiceAt(Dx21VoiceLibrary& library, const int bankIndex, const int voiceIndex)
{
    validateBankIndex(bankIndex);
    validateVoiceIndex(voiceIndex);
    return library.banks[static_cast<std::size_t>(bankIndex)].voices[static_cast<std::size_t>(voiceIndex)];
}
} // namespace dx21
