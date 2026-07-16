#include "App/OpalineVoiceLibraryXml.h"

#include <array>
#include <cstring>

namespace opalineapp
{
namespace
{
juce::ValueTree voiceToValueTree(const opaline::OpalinePatchWithMetadata& voice, const int index)
{
    juce::ValueTree tree { "Voice" };
    tree.setProperty("index", index, nullptr);
    tree.setProperty("name", juce::String(voice.name), nullptr);

    const auto vmem = opaline::encodeCompatibleVmemVoice(voice);
    juce::MemoryBlock block(vmem.data(), vmem.size());
    tree.setProperty("vmem", block.toBase64Encoding(), nullptr);

    const auto effects = opaline::normalizePatch(voice.patch).effects;
    juce::ValueTree effectsTree { "Effects" };
    effectsTree.setProperty("enabled", voice.effectsEnabled, nullptr);
    effectsTree.setProperty("reverb", effects.reverb, nullptr);
    effectsTree.setProperty("mix", effects.mix, nullptr);
    effectsTree.setProperty("delay", effects.delay, nullptr);
    effectsTree.setProperty("echoMix", effects.echoMix, nullptr);
    effectsTree.setProperty("chorus", effects.chorus, nullptr);
    effectsTree.setProperty("tone", effects.tone, nullptr);
    tree.addChild(effectsTree, -1, nullptr);
    return tree;
}

juce::ValueTree bankToValueTree(const opaline::OpalineVoiceBank& bank, const int index)
{
    juce::ValueTree tree { "Bank" };
    tree.setProperty("index", index, nullptr);
    tree.setProperty("name", juce::String(bank.name), nullptr);

    for (int i = 0; i < opaline::kOpalineVoiceBankSize; ++i)
        tree.addChild(voiceToValueTree(bank.voices[static_cast<std::size_t>(i)], i), -1, nullptr);

    return tree;
}
} // namespace

std::unique_ptr<juce::XmlElement> voiceLibraryToXml(const opaline::OpalineVoiceLibrary& library)
{
    juce::ValueTree tree { "compatibleVoiceLibrary" };
    tree.setProperty("version", 2, nullptr);
    for (int i = 0; i < opaline::kOpalineVoiceBankCount; ++i)
        tree.addChild(bankToValueTree(library.banks[static_cast<std::size_t>(i)], i), -1, nullptr);

    return tree.createXml();
}

bool voiceLibraryFromXml(const juce::XmlElement& xml, opaline::OpalineVoiceLibrary& library)
{
    const auto tree = juce::ValueTree::fromXml(xml);
    if (!tree.hasType("compatibleVoiceLibrary"))
        return false;

    auto restored = opaline::makeInitVoiceLibrary();
    for (int childIndex = 0; childIndex < tree.getNumChildren(); ++childIndex)
    {
        const auto bankTree = tree.getChild(childIndex);
        if (!bankTree.hasType("Bank"))
            continue;

        const int bankIndex = juce::jlimit(0,
                                           opaline::kOpalineVoiceBankCount - 1,
                                           static_cast<int>(bankTree.getProperty("index", childIndex)));
        auto& bank = restored.banks[static_cast<std::size_t>(bankIndex)];
        const auto bankName = bankTree.getProperty("name").toString();
        if (bankName.isNotEmpty())
            bank.name = bankName.toStdString();

        for (int voiceChild = 0; voiceChild < bankTree.getNumChildren(); ++voiceChild)
        {
            const auto voiceTree = bankTree.getChild(voiceChild);
            if (!voiceTree.hasType("Voice"))
                continue;

            const int voiceIndex = juce::jlimit(0,
                                                opaline::kOpalineVoiceBankSize - 1,
                                                static_cast<int>(voiceTree.getProperty("index", voiceChild)));
            juce::MemoryBlock block;
            if (!block.fromBase64Encoding(voiceTree.getProperty("vmem").toString())
                || block.getSize() != static_cast<std::size_t>(opaline::kOpalineVmemVoiceSize))
            {
                continue;
            }

            std::array<std::uint8_t, opaline::kOpalineVmemVoiceSize> vmem {};
            std::memcpy(vmem.data(), block.getData(), vmem.size());
            auto voice = opaline::decodeCompatibleVmemVoice(vmem);
            const auto name = voiceTree.getProperty("name").toString();
            if (name.isNotEmpty())
                voice.name = name.toStdString();

            const auto effectsTree = voiceTree.getChildWithName("Effects");
            if (effectsTree.isValid())
            {
                voice.effectsEnabled = static_cast<bool>(effectsTree.getProperty("enabled", true));
                voice.patch.effects.reverb = static_cast<int>(effectsTree.getProperty("reverb", 0));
                voice.patch.effects.mix = static_cast<int>(effectsTree.getProperty("mix", 0));
                voice.patch.effects.delay = static_cast<int>(effectsTree.getProperty("delay", 0));
                voice.patch.effects.echoMix = static_cast<int>(effectsTree.getProperty("echoMix", 0));
                voice.patch.effects.chorus = static_cast<int>(effectsTree.getProperty("chorus", 0));
                voice.patch.effects.tone = static_cast<int>(effectsTree.getProperty("tone", 50));
                voice.patch = opaline::normalizePatch(voice.patch);
            }
            bank.voices[static_cast<std::size_t>(voiceIndex)] = voice;
        }
    }

    library = std::move(restored);
    return true;
}
} // namespace opalineapp
