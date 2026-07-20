#include "App/OpalineVoiceLibraryXml.h"
#include "Engine/OpalineSysex.h"
#include "Engine/OpalineVoiceLibrary.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using Voice = opaline::OpalinePatchWithMetadata;

constexpr std::array<const char*, opaline::kOpalineVoiceBankSize> kVariantNames {{
    "MistGrand", "NovaKey", "FrostTine", "TimberKey", "HaloStrike", "RotorBlue", "QuietWind", "SatinBow",
    "PinPluck", "BrassTide", "MellowAir", "OrbitLead", "PulseLead", "NightGtr", "CloudHarp", "StarGlass",
    "TineBrass", "AuraVibe", "RoseMallet", "PulseDrum", "GearTone", "DeepTimp", "BoldBrass", "HollowAir",
    "NeonReed", "AmberPipe", "SoftBell", "VoxCloud", "AnchorBass", "SnapBass2", "TonePiano", "SlapRoot",
}};

int nudge(const int value, const int minimum, const int maximum, const int amount, const int direction)
{
    const int candidate = value + amount * direction;
    if (candidate >= minimum && candidate <= maximum)
        return candidate;
    return value - amount * direction;
}

std::vector<std::uint8_t> readBinary(const juce::File& file)
{
    juce::MemoryBlock data;
    if (!file.loadFileAsData(data))
        throw std::runtime_error("Could not read " + file.getFullPathName().toStdString());
    const auto* begin = static_cast<const std::uint8_t*>(data.getData());
    return { begin, begin + data.getSize() };
}

opaline::OpalineVoiceLibrary readEffectsLibrary(const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
        throw std::runtime_error("Could not parse " + file.getFullPathName().toStdString());

    opaline::OpalineVoiceLibrary library;
    if (!opalineapp::voiceLibraryFromXml(*xml, library))
        throw std::runtime_error("Unsupported voice library XML.");
    return library;
}

std::vector<int> numericParameters(const opaline::OpalinePatch& patch)
{
    std::vector<int> values {
        patch.algorithm, patch.feedback, patch.transpose,
        patch.lfo.speed, patch.lfo.delay, patch.lfo.pitchDepth, patch.lfo.ampDepth,
        patch.lfo.pitchSensitivity, patch.lfo.ampSensitivity, patch.lfo.wave,
        patch.pitchEnvelope.rate1, patch.pitchEnvelope.rate2, patch.pitchEnvelope.rate3,
        patch.pitchEnvelope.level1, patch.pitchEnvelope.level2, patch.pitchEnvelope.level3,
        patch.effects.reverb, patch.effects.mix, patch.effects.delay,
        patch.effects.echoMix, patch.effects.chorus, patch.effects.tone,
    };
    for (const auto& op : patch.operators)
    {
        values.insert(values.end(), {
            op.ratioIndex, op.detune, op.level, op.rateScale, op.levelScale, op.velocity,
            op.envelope.attackRate, op.envelope.decay1Rate, op.envelope.decay1Level,
            op.envelope.decay2Rate, op.envelope.releaseRate,
        });
    }
    return values;
}

int countChangedNumericParameters(const opaline::OpalinePatch& before, const opaline::OpalinePatch& after)
{
    const auto left = numericParameters(before);
    const auto right = numericParameters(after);
    if (left.size() != 66 || right.size() != left.size())
        throw std::runtime_error("Unexpected editable parameter count.");

    int changed = 0;
    for (std::size_t i = 0; i < left.size(); ++i)
        changed += left[i] != right[i] ? 1 : 0;
    return changed;
}

Voice makeVariant(const Voice& source, const int voiceIndex)
{
    Voice result = source;
    result.name = kVariantNames[static_cast<std::size_t>(voiceIndex)];
    result.hasVmem = false;
    result.vmem = {};
    auto& patch = result.patch;
    const int voiceDirection = (voiceIndex % 2 == 0) ? 1 : -1;

    patch.feedback = nudge(patch.feedback, 0, 7, 1, voiceDirection);
    patch.lfo.speed = nudge(patch.lfo.speed, 0, 99, 2, voiceDirection);
    patch.lfo.delay = nudge(patch.lfo.delay, 0, 99, 2, -voiceDirection);
    patch.lfo.pitchDepth = nudge(patch.lfo.pitchDepth, 0, 99, 1, voiceDirection);
    patch.lfo.ampDepth = nudge(patch.lfo.ampDepth, 0, 99, 1, -voiceDirection);

    patch.pitchEnvelope.rate1 = nudge(patch.pitchEnvelope.rate1, 0, 99, 1, voiceDirection);
    patch.pitchEnvelope.rate2 = nudge(patch.pitchEnvelope.rate2, 0, 99, 1, -voiceDirection);
    patch.pitchEnvelope.rate3 = nudge(patch.pitchEnvelope.rate3, 0, 99, 1, voiceDirection);
    patch.pitchEnvelope.level1 = nudge(patch.pitchEnvelope.level1, 0, 99, 1, -voiceDirection);
    patch.pitchEnvelope.level2 = nudge(patch.pitchEnvelope.level2, 0, 99, 1, voiceDirection);
    patch.pitchEnvelope.level3 = nudge(patch.pitchEnvelope.level3, 0, 99, 1, -voiceDirection);

    patch.effects.reverb = nudge(patch.effects.reverb, 0, 99, 2, voiceDirection);
    patch.effects.mix = nudge(patch.effects.mix, 0, 99, 1, -voiceDirection);
    patch.effects.delay = nudge(patch.effects.delay, 0, 99, 2, voiceDirection);
    patch.effects.echoMix = nudge(patch.effects.echoMix, 0, 99, 1, -voiceDirection);
    patch.effects.chorus = nudge(patch.effects.chorus, 0, 99, 1, voiceDirection);
    patch.effects.tone = nudge(patch.effects.tone, 0, 99, 2, -voiceDirection);

    for (int opIndex = 0; opIndex < opaline::kOperatorCount; ++opIndex)
    {
        auto& op = patch.operators[static_cast<std::size_t>(opIndex)];
        const int direction = ((voiceIndex + opIndex) % 2 == 0) ? 1 : -1;
        if (opIndex == 1 || opIndex == 3)
            op.ratioIndex = nudge(op.ratioIndex, 0, 63, 1, direction);
        op.detune = nudge(op.detune, -3, 3, 1, direction);
        op.level = nudge(op.level, 0, 99, op.level >= 80 ? 1 : 2, -direction);
        if (opIndex == 1 || opIndex == 3)
            op.rateScale = nudge(op.rateScale, 0, 3, 1, direction);
        op.levelScale = nudge(op.levelScale, 0, 99, 2, -direction);
        op.velocity = nudge(op.velocity, 0, 7, 1, direction);
        op.envelope.attackRate = nudge(op.envelope.attackRate, 0, 31, 1, -direction);
        op.envelope.decay1Rate = nudge(op.envelope.decay1Rate, 0, 31, 1, direction);
        op.envelope.decay1Level = nudge(op.envelope.decay1Level, 0, 15, 1, -direction);
        op.envelope.decay2Rate = nudge(op.envelope.decay2Rate, 0, 31, 1, direction);
        op.envelope.releaseRate = nudge(op.envelope.releaseRate, 0, 15, 1, -direction);
    }

    result.patch = opaline::normalizePatch(patch);
    result.vmem = opaline::encodeCompatibleVmemVoice(result);
    result.hasVmem = true;
    return result;
}

void writeBinary(const juce::File& file, const std::vector<std::uint8_t>& bytes)
{
    if (!file.getParentDirectory().createDirectory() || !file.replaceWithData(bytes.data(), bytes.size()))
        throw std::runtime_error("Could not write " + file.getFullPathName().toStdString());
}

void writeXml(const juce::File& file, const std::vector<Voice>& voices)
{
    auto library = opaline::makeInitVoiceLibrary();
    library.banks[0].name = "Factory Variant v1";
    for (int i = 0; i < opaline::kOpalineVoiceBankSize; ++i)
        library.banks[0].voices[static_cast<std::size_t>(i)] = voices[static_cast<std::size_t>(i)];

    auto xml = opalineapp::voiceLibraryToXml(library);
    xml->setAttribute("source", "factory_variant_v1.syx");
    for (int bankIndex = 1; bankIndex < opaline::kOpalineVoiceBankCount; ++bankIndex)
    {
        if (auto* bank = xml->getChildElement(bankIndex))
            bank->deleteAllChildElements();
    }

    opaline::OpalineVoiceLibrary restored;
    if (!opalineapp::voiceLibraryFromXml(*xml, restored))
        throw std::runtime_error("Generated variant XML could not be decoded.");
    for (int i = 0; i < opaline::kOpalineVoiceBankSize; ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        if (opaline::encodeCompatibleVmemVoice(restored.banks[0].voices[index])
            != opaline::encodeCompatibleVmemVoice(voices[index]))
        {
            throw std::runtime_error("Variant XML and SysEx differ at voice " + std::to_string(i + 1));
        }
    }

    if (!file.getParentDirectory().createDirectory()
        || !file.replaceWithText(xml->toString(), false, false, "\n"))
        throw std::runtime_error("Could not write " + file.getFullPathName().toStdString());
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const auto root = argc >= 2 ? juce::File(juce::String::fromUTF8(argv[1]))
                                    : juce::File::getCurrentWorkingDirectory();
        const auto assets = root.getChildFile("assets");
        const auto presets = opaline::parseCompatibleBulkVmem(readBinary(assets.getChildFile("factory.syx")));
        const auto effectsLibrary = readEffectsLibrary(assets.getChildFile("factory.opalinelibrary.xml"));

        std::vector<Voice> variants;
        variants.reserve(opaline::kOpalineVoiceBankSize);
        std::set<std::string> names;
        for (int i = 0; i < opaline::kOpalineVoiceBankSize; ++i)
        {
            auto source = opaline::decodeCompatibleVmemVoice(presets[static_cast<std::size_t>(i)].vmem);
            source.patch.effects = effectsLibrary.banks[0].voices[static_cast<std::size_t>(i)].patch.effects;
            source.effectsEnabled = effectsLibrary.banks[0].voices[static_cast<std::size_t>(i)].effectsEnabled;
            auto variant = makeVariant(source, i);
            const int changed = countChangedNumericParameters(source.patch, variant.patch);
            if (changed < 53 || changed > 59)
                throw std::runtime_error("Voice " + std::to_string(i + 1) + " changed outside the 80-90% target.");
            if (!names.insert(variant.name).second)
                throw std::runtime_error("Variant voice names must be unique.");
            variants.push_back(std::move(variant));
            std::cout << (i + 1) << ". " << variants.back().name
                      << " - " << changed << "/66 numeric parameters changed\n";
        }

        const auto sysex = opaline::encodeCompatibleBulkVmem(variants);
        if (opaline::parseCompatibleBulkVmem(sysex).size() != variants.size())
            throw std::runtime_error("Generated variant SysEx validation failed.");
        writeBinary(assets.getChildFile("factory_variant_v1.syx"), sysex);
        writeXml(assets.getChildFile("factory_variant_v1.opalinelibrary.xml"), variants);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
