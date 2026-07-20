#include "App/OpalineVoiceLibraryXml.h"
#include "Engine/OpalineSysex.h"
#include "Engine/OpalineVoiceLibrary.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
struct FactoryEffects
{
    int reverb;
    int reverbMix;
    int delay;
    int delayMix;
    int chorus;
    int tone;
};

constexpr std::array<FactoryEffects, opaline::kOpalineVoiceBankSize> kFactoryEffects {{
    { 46, 20,  0,  0,  5, 52 }, // GrandPiano
    { 38, 16,  0,  0, 10, 58 }, // NeoElectro
    { 50, 24, 18,  8, 12, 62 }, // IceTines
    { 38, 16,  0,  0,  5, 50 }, // CedarKeys
    { 70, 34, 18,  8,  6, 65 }, // HaloBells
    { 35, 17,  0,  0, 35, 50 }, // BlueRotor
    { 62, 28,  0,  0,  4, 45 }, // QuietPipe
    { 55, 24,  0,  0, 22, 48 }, // SatinStrng
    { 32, 14, 22, 10,  3, 62 }, // PinchPluk
    { 42, 18,  0,  0,  8, 55 }, // BrassWave
    { 48, 21,  0,  0, 10, 45 }, // MellowHrn
    { 58, 27, 32, 16, 14, 60 }, // SpaceLead
    { 38, 17, 24, 12,  8, 58 }, // FMLead
    { 45, 20,  0,  0, 12, 40 }, // NightJazz
    { 68, 32, 16,  7, 15, 58 }, // CloudHarp
    { 72, 35, 22, 10,  9, 68 }, // StarChime
    { 46, 21, 10,  5, 13, 55 }, // Tinesbrass
    { 58, 27, 12,  6, 17, 56 }, // AuraVibes
    { 48, 22,  8,  4,  8, 58 }, // RoseMallet
    { 24, 11,  0,  0,  0, 48 }, // PulseTom
    { 30, 13, 16,  7,  6, 60 }, // GearPulse
    { 34, 15,  0,  0,  0, 42 }, // DeepKettle
    { 38, 16,  0,  0,  5, 55 }, // BossBrass
    { 64, 30, 20,  9, 18, 52 }, // HollowSky
    { 36, 15,  0,  0,  6, 56 }, // SuperBrass
    { 50, 22,  0,  0, 10, 45 }, // AmberReed
    { 65, 30, 12,  5, 12, 64 }, // SoftCelest
    { 70, 33, 16,  7, 20, 48 }, // VoxMist
    { 18,  7,  0,  0,  2, 42 }, // AnchorBass
    { 14,  5,  0,  0,  0, 55 }, // SnapBass
    { 10,  3,  0,  0,  2, 45 }, // SuperBass
    { 18,  7,  0,  0,  3, 57 }, // SlapCore
}};

std::vector<std::uint8_t> readBinary(const juce::File& file)
{
    juce::MemoryBlock data;
    if (!file.loadFileAsData(data))
        throw std::runtime_error("Could not read " + file.getFullPathName().toStdString());
    const auto* begin = static_cast<const std::uint8_t*>(data.getData());
    return { begin, begin + data.getSize() };
}

void applyEffects(opaline::OpalinePatchWithMetadata& voice, const FactoryEffects& settings)
{
    voice.patch.effects.reverb = settings.reverb;
    voice.patch.effects.mix = settings.reverbMix;
    voice.patch.effects.delay = settings.delay;
    voice.patch.effects.echoMix = settings.delayMix;
    voice.patch.effects.chorus = settings.chorus;
    voice.patch.effects.tone = settings.tone;
    voice.effectsEnabled = settings.reverbMix > 0 || settings.delayMix > 0
        || settings.chorus > 0 || settings.tone != 50;
    voice.patch = opaline::normalizePatch(voice.patch);
}

int applyMinimumVelocityResponse(opaline::OpalineVoiceBank& bank)
{
    int corrected = 0;
    for (auto& voice : bank.voices)
    {
        for (auto& op : voice.patch.operators)
        {
            if (op.level > 0 && op.velocity == 0)
            {
                op.velocity = 1;
                ++corrected;
            }
        }
    }
    return corrected;
}

void writeBinary(const juce::File& file, const std::vector<std::uint8_t>& bytes)
{
    if (!file.replaceWithData(bytes.data(), bytes.size()))
        throw std::runtime_error("Could not write " + file.getFullPathName().toStdString());
}

void validateRoundTrip(const juce::XmlElement& xml,
                       const std::array<opaline::OpalinePatchWithMetadata, opaline::kOpalineVoiceBankSize>& voices)
{
    opaline::OpalineVoiceLibrary restored;
    if (!opalineapp::voiceLibraryFromXml(xml, restored))
        throw std::runtime_error("Generated factory XML could not be decoded.");

    for (int i = 0; i < opaline::kOpalineVoiceBankSize; ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        const auto& expected = voices[index];
        const auto& actual = restored.banks[0].voices[index];
        if (actual.name != expected.name
            || actual.effectsEnabled != expected.effectsEnabled
            || opaline::encodeCompatibleVmemVoice(actual) != opaline::encodeCompatibleVmemVoice(expected)
            || actual.patch.effects.reverb != expected.patch.effects.reverb
            || actual.patch.effects.mix != expected.patch.effects.mix
            || actual.patch.effects.delay != expected.patch.effects.delay
            || actual.patch.effects.echoMix != expected.patch.effects.echoMix
            || actual.patch.effects.chorus != expected.patch.effects.chorus
            || actual.patch.effects.tone != expected.patch.effects.tone)
        {
            throw std::runtime_error("Factory XML round trip failed at voice " + std::to_string(i + 1));
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const auto root = argc >= 2 ? juce::File(juce::String::fromUTF8(argv[1]))
                                    : juce::File::getCurrentWorkingDirectory();
        const auto assets = root.getChildFile("assets");
        const auto input = assets.getChildFile("factory_v1.syx");
        const auto output = assets.getChildFile("factory.opalinelibrary.xml");
        const auto bank = opaline::voiceBankFromSysex(readBinary(input), "factory");

        auto library = opaline::makeInitVoiceLibrary();
        library.banks[0] = bank;
        const int correctedVelocityParameters = applyMinimumVelocityResponse(library.banks[0]);
        for (int i = 0; i < opaline::kOpalineVoiceBankSize; ++i)
            applyEffects(library.banks[0].voices[static_cast<std::size_t>(i)], kFactoryEffects[static_cast<std::size_t>(i)]);

        auto xml = opalineapp::voiceLibraryToXml(library);
        xml->setAttribute("source", "factory.syx");
        for (int bankIndex = 1; bankIndex < opaline::kOpalineVoiceBankCount; ++bankIndex)
        {
            if (auto* emptyBank = xml->getChildElement(bankIndex))
                emptyBank->deleteAllChildElements();
        }
        validateRoundTrip(*xml, library.banks[0].voices);

        writeBinary(assets.getChildFile("factory.syx"), opaline::voiceBankToSysex(library.banks[0]));
        if (!output.replaceWithText(xml->toString(), false, false, "\n"))
            throw std::runtime_error("Could not write " + output.getFullPathName().toStdString());

        std::cout << "Generated default library with 32 Bank 1 voices and per-voice effects.\n";
        std::cout << "Corrected " << correctedVelocityParameters
                  << " active operator velocity values from 0 to 1.\n";
        for (int i = 0; i < opaline::kOpalineVoiceBankSize; ++i)
        {
            const auto& voice = library.banks[0].voices[static_cast<std::size_t>(i)];
            std::cout << (i + 1) << ". " << voice.name
                      << " - Reverb " << voice.patch.effects.reverb
                      << "/" << voice.patch.effects.mix
                      << ", Delay " << voice.patch.effects.delay
                      << "/" << voice.patch.effects.echoMix
                      << ", Chorus " << voice.patch.effects.chorus
                      << ", Tone " << voice.patch.effects.tone << '\n';
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
