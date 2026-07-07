#include "MainComponent.h"

#include "App/OpalineStateSerialization.h"
#include "Engine/OpalineTables.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace
{
constexpr int kFirstKeyboardNote = 48;
constexpr int kKeyboardNoteCount = 37;
constexpr int kPcKeyboardTranspose = 12;
constexpr int kPreferredAudioBufferSize = 128;
constexpr int kMaxLowLatencyAudioBufferSize = 128;
constexpr std::array<int, 4> kLowLatencyAudioBufferSizes { 32, 64, 96, 128 };
constexpr float kAsioOutputTrim = 0.50f;

struct PcKeyNote
{
    int keyCode;
    int note;
};

constexpr std::array<PcKeyNote, 41> kPcKeyboardMap {{
    { 'z', 36 }, { 'x', 38 }, { 'c', 40 }, { 'v', 41 }, { 'b', 43 }, { 'n', 45 }, { 'm', 47 },
    { ',', 48 }, { '.', 50 }, { '/', 52 }, { '\\', 53 },
    { 's', 37 }, { 'd', 39 }, { 'g', 42 }, { 'h', 44 }, { 'j', 46 }, { 'l', 49 }, { ';', 51 }, { ':', 51 },
    { 'q', 48 }, { 'w', 50 }, { 'e', 52 }, { 'r', 53 }, { 't', 55 }, { 'y', 57 }, { 'u', 59 },
    { 'i', 60 }, { 'o', 62 }, { 'p', 64 }, { '@', 65 }, { '[', 67 }, { ']', 67 },
    { '2', 49 }, { '3', 51 }, { '5', 54 }, { '6', 56 }, { '7', 58 }, { '9', 61 }, { '0', 63 },
    { '-', 66 }, { '^', 66 }
}};

bool isPcKeyCurrentlyDown(const int keyCode)
{
    if (keyCode >= 'a' && keyCode <= 'z')
        return juce::KeyPress::isKeyCurrentlyDown(keyCode)
            || juce::KeyPress::isKeyCurrentlyDown(keyCode - 'a' + 'A');

    return juce::KeyPress::isKeyCurrentlyDown(keyCode);
}

std::vector<std::uint8_t> readBinaryFile(const juce::File& file)
{
    std::ifstream input(file.getFullPathName().toStdString(), std::ios::binary);
    if (!input)
        return {};

    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

bool writeBinaryFile(const juce::File& file, const std::vector<std::uint8_t>& bytes)
{
    file.deleteFile();
    juce::FileOutputStream output(file);
    if (!output.openedOk())
        return false;

    return output.write(bytes.data(), static_cast<int>(bytes.size()));
}

juce::ValueTree voiceToValueTree(const opaline::OpalinePatchWithMetadata& voice, const int index)
{
    juce::ValueTree tree { "Voice" };
    tree.setProperty("index", index, nullptr);
    tree.setProperty("name", juce::String(voice.name), nullptr);

    const auto vmem = opaline::encodeCompatibleVmemVoice(voice);
    juce::MemoryBlock block(vmem.data(), vmem.size());
    tree.setProperty("vmem", block.toBase64Encoding(), nullptr);
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

std::unique_ptr<juce::XmlElement> voiceLibraryToXml(const opaline::OpalineVoiceLibrary& library)
{
    juce::ValueTree tree { "compatibleVoiceLibrary" };
    tree.setProperty("version", 1, nullptr);
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
            bank.voices[static_cast<std::size_t>(voiceIndex)] = voice;
        }
    }

    library = std::move(restored);
    return true;
}

juce::String displayNameForVoice(int index, const opaline::OpalinePatchWithMetadata& voice)
{
    const auto number = juce::String(index + 1).paddedLeft('0', 2);
    const auto name = voice.name.empty() ? juce::String("VOICE") : juce::String(voice.name);
    return "A" + number + " " + name;
}

bool isBlackKey(int midiNote)
{
    switch (midiNote % 12)
    {
        case 1:
        case 3:
        case 6:
        case 8:
        case 10:
            return true;
        default:
            return false;
    }
}

int whiteKeyIndexForNote(const int midiNote)
{
    int index = 0;
    for (int note = kFirstKeyboardNote; note < midiNote; ++note)
    {
        if (!isBlackKey(note))
            ++index;
    }

    return index;
}

int noteForWhiteKeyIndex(const int whiteIndex)
{
    int index = 0;
    for (int note = kFirstKeyboardNote; note < kFirstKeyboardNote + kKeyboardNoteCount; ++note)
    {
        if (isBlackKey(note))
            continue;

        if (index == whiteIndex)
            return note;

        ++index;
    }

    return kFirstKeyboardNote + kKeyboardNoteCount - 1;
}

juce::String midiNoteName(const int note)
{
    static constexpr std::array<const char*, 12> names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int safeNote = juce::jlimit(0, 127, note);
    return juce::String(names[static_cast<std::size_t>(safeNote % 12)]) + juce::String(safeNote / 12 - 1);
}

#if JUCE_WINDOWS
bool isWasapiTypeName(const juce::String& typeName)
{
    return typeName.startsWith("Windows Audio");
}

bool isStrictLowLatencyTypeName(const juce::String& typeName)
{
    return isWasapiTypeName(typeName);
}

bool isUnsafeAsioDeviceName(const juce::String& deviceName)
{
    const auto name = deviceName.toLowerCase();
    return name.contains("built-in")
        || name.contains("generic")
        || name.contains("low latency")
        || name.contains("’áƒŒƒCƒeƒ“ƒV");
}

std::vector<juce::String> wasapiLowLatencyTypeOrder(const juce::String& requestedTypeName)
{
    std::vector<juce::String> types;

    auto addType = [&types](const juce::String& typeName)
    {
        if (typeName.isNotEmpty()
            && std::find(types.begin(), types.end(), typeName) == types.end())
        {
            types.push_back(typeName);
        }
    };

    if (requestedTypeName == "Windows Audio (Exclusive Mode)")
        addType(requestedTypeName);
    else
        addType("Windows Audio (Exclusive Mode)");

    addType(requestedTypeName);
    addType("Windows Audio (Low Latency Mode)");
    return types;
}
#endif

juce::String lfoWaveName(const int wave)
{
    static constexpr std::array<const char*, 4> names { "SAW UP", "SQUARE", "TRIANGLE", "S/H" };
    return names[static_cast<std::size_t>(juce::jlimit(0, 3, wave))];
}

juce::String operatorRole(const opaline::OpalinePatch& patch, const int opIndex)
{
    const auto& algorithm = opaline::opalineAlgorithms()[static_cast<std::size_t>(juce::jlimit(1, 8, patch.algorithm) - 1)];
    bool carrier = false;
    juce::StringArray targets;
    for (int i = 0; i < algorithm.carrierCount; ++i)
        carrier = carrier || algorithm.carriers[static_cast<std::size_t>(i)] == opIndex;

    for (int target = 0; target < opaline::kOperatorCount; ++target)
    {
        for (int dep = 0; dep < algorithm.depCounts[static_cast<std::size_t>(target)]; ++dep)
        {
            if (algorithm.deps[static_cast<std::size_t>(target)][static_cast<std::size_t>(dep)] == opIndex)
                targets.add("OP" + juce::String(target + 1));
        }
    }

    if (carrier && !targets.isEmpty())
        return "Carrier + Mod -> " + targets.joinIntoString(",");
    if (carrier)
        return "Carrier";
    if (!targets.isEmpty())
        return "Mod -> " + targets.joinIntoString(",");
    return "Inactive";
}

float egTimeWeight(const int rate, const int maxRate)
{
    const float normalized = static_cast<float>(juce::jlimit(0, maxRate, rate)) / static_cast<float>(maxRate);
    return 0.12f + (1.0f - normalized) * (1.0f - normalized) * 0.88f;
}

float pegLevelToCents(const int level)
{
    const int value = juce::jlimit(0, 99, level);
    if (value >= 50)
        return static_cast<float>(value - 50) * 4800.0f / 49.0f;

    return static_cast<float>(value - 50) * 4800.0f / 50.0f;
}

float pegRateWeight(const int rate)
{
    const float normalized = static_cast<float>(juce::jlimit(0, 99, rate)) / 99.0f;
    return 0.12f + (1.0f - normalized) * (1.0f - normalized) * 0.88f;
}

const juce::Colour kAppBackground { 0xff020405 };
const juce::Colour kPanelTop { 0xff29261f };
const juce::Colour kPanelBottom { 0xff14130f };
const juce::Colour kPanelBorder { 0xff343126 };
const juce::Colour kPanelBorderOn { 0xff6f726a };
const juce::Colour kControlWell { 0xff050606 };
const juce::Colour kControlBorder { 0xff282820 };
const juce::Colour kTextPrimary { 0xffedf4f3 };
const juce::Colour kTextMuted { 0xffb7c2bd };
const juce::Colour kValueText { 0xffffd52b };
const juce::Colour kTeal { 0xff25d9c4 };
const juce::Colour kGreen { 0xff35e87e };
const juce::Colour kEnvelope { 0xffead39c };
std::array<std::atomic<bool>, 128> gMidiUiHeldNotes {};
std::array<std::atomic<int>, 128> gMidiUiHeldVelocities {};

constexpr const char* kMidiInputIdentifierSetting = "midiInputIdentifier";
constexpr const char* kAudioOutputTypeSetting = "audioOutputType";
constexpr const char* kAudioOutputDeviceSetting = "audioOutputDevice";
constexpr const char* kVoiceLibraryXmlSetting = "voiceLibraryXml";
constexpr const char* kVoiceBankIndexSetting = "voiceBankIndex";
constexpr const char* kVoiceAIndexSetting = "voiceAIndex";
constexpr const char* kVoiceBIndexSetting = "voiceBIndex";
constexpr const char* kPerformanceModeSetting = "performanceMode";
constexpr const char* kDualDetuneSetting = "dualDetune";
constexpr const char* kSplitPointSetting = "splitPoint";

juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Opaline FM";
    options.filenameSuffix = ".settings";
    options.folderName = "Opaline FM";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.millisecondsBeforeSaving = 0;
    return options;
}

std::array<std::uint8_t, 5> lcdGlyph(juce::juce_wchar character)
{
    switch (character)
    {
        case '0': return { 0x3e, 0x51, 0x49, 0x45, 0x3e };
        case '1': return { 0x00, 0x42, 0x7f, 0x40, 0x00 };
        case '2': return { 0x42, 0x61, 0x51, 0x49, 0x46 };
        case '3': return { 0x21, 0x41, 0x45, 0x4b, 0x31 };
        case '4': return { 0x18, 0x14, 0x12, 0x7f, 0x10 };
        case '5': return { 0x27, 0x45, 0x45, 0x45, 0x39 };
        case '6': return { 0x3c, 0x4a, 0x49, 0x49, 0x30 };
        case '7': return { 0x01, 0x71, 0x09, 0x05, 0x03 };
        case '8': return { 0x36, 0x49, 0x49, 0x49, 0x36 };
        case '9': return { 0x06, 0x49, 0x49, 0x29, 0x1e };
        case 'A': return { 0x7e, 0x11, 0x11, 0x11, 0x7e };
        case 'B': return { 0x7f, 0x49, 0x49, 0x49, 0x36 };
        case 'C': return { 0x3e, 0x41, 0x41, 0x41, 0x22 };
        case 'D': return { 0x7f, 0x41, 0x41, 0x22, 0x1c };
        case 'E': return { 0x7f, 0x49, 0x49, 0x49, 0x41 };
        case 'F': return { 0x7f, 0x09, 0x09, 0x09, 0x01 };
        case 'G': return { 0x3e, 0x41, 0x49, 0x49, 0x7a };
        case 'H': return { 0x7f, 0x08, 0x08, 0x08, 0x7f };
        case 'I': return { 0x00, 0x41, 0x7f, 0x41, 0x00 };
        case 'J': return { 0x20, 0x40, 0x41, 0x3f, 0x01 };
        case 'K': return { 0x7f, 0x08, 0x14, 0x22, 0x41 };
        case 'L': return { 0x7f, 0x40, 0x40, 0x40, 0x40 };
        case 'M': return { 0x7f, 0x02, 0x0c, 0x02, 0x7f };
        case 'N': return { 0x7f, 0x04, 0x08, 0x10, 0x7f };
        case 'O': return { 0x3e, 0x41, 0x41, 0x41, 0x3e };
        case 'P': return { 0x7f, 0x09, 0x09, 0x09, 0x06 };
        case 'Q': return { 0x3e, 0x41, 0x51, 0x21, 0x5e };
        case 'R': return { 0x7f, 0x09, 0x19, 0x29, 0x46 };
        case 'S': return { 0x46, 0x49, 0x49, 0x49, 0x31 };
        case 'T': return { 0x01, 0x01, 0x7f, 0x01, 0x01 };
        case 'U': return { 0x3f, 0x40, 0x40, 0x40, 0x3f };
        case 'V': return { 0x1f, 0x20, 0x40, 0x20, 0x1f };
        case 'W': return { 0x3f, 0x40, 0x38, 0x40, 0x3f };
        case 'X': return { 0x63, 0x14, 0x08, 0x14, 0x63 };
        case 'Y': return { 0x07, 0x08, 0x70, 0x08, 0x07 };
        case 'Z': return { 0x61, 0x51, 0x49, 0x45, 0x43 };
        case 'a': return { 0x20, 0x54, 0x54, 0x54, 0x78 };
        case 'b': return { 0x7f, 0x48, 0x44, 0x44, 0x38 };
        case 'c': return { 0x38, 0x44, 0x44, 0x44, 0x20 };
        case 'd': return { 0x38, 0x44, 0x44, 0x48, 0x7f };
        case 'e': return { 0x38, 0x54, 0x54, 0x54, 0x18 };
        case 'f': return { 0x08, 0x7e, 0x09, 0x01, 0x02 };
        case 'g': return { 0x0c, 0x52, 0x52, 0x52, 0x3e };
        case 'h': return { 0x7f, 0x08, 0x04, 0x04, 0x78 };
        case 'i': return { 0x00, 0x44, 0x7d, 0x40, 0x00 };
        case 'j': return { 0x20, 0x40, 0x44, 0x3d, 0x00 };
        case 'k': return { 0x7f, 0x10, 0x28, 0x44, 0x00 };
        case 'l': return { 0x00, 0x41, 0x7f, 0x40, 0x00 };
        case 'm': return { 0x7c, 0x04, 0x18, 0x04, 0x78 };
        case 'n': return { 0x7c, 0x08, 0x04, 0x04, 0x78 };
        case 'o': return { 0x38, 0x44, 0x44, 0x44, 0x38 };
        case 'p': return { 0x7c, 0x14, 0x14, 0x14, 0x08 };
        case 'q': return { 0x08, 0x14, 0x14, 0x18, 0x7c };
        case 'r': return { 0x7c, 0x08, 0x04, 0x04, 0x08 };
        case 's': return { 0x48, 0x54, 0x54, 0x54, 0x20 };
        case 't': return { 0x04, 0x3f, 0x44, 0x40, 0x20 };
        case 'u': return { 0x3c, 0x40, 0x40, 0x20, 0x7c };
        case 'v': return { 0x1c, 0x20, 0x40, 0x20, 0x1c };
        case 'w': return { 0x3c, 0x40, 0x30, 0x40, 0x3c };
        case 'x': return { 0x44, 0x28, 0x10, 0x28, 0x44 };
        case 'y': return { 0x0c, 0x50, 0x50, 0x50, 0x3c };
        case 'z': return { 0x44, 0x64, 0x54, 0x4c, 0x44 };
        case '-': return { 0x08, 0x08, 0x08, 0x08, 0x08 };
        case '_': return { 0x40, 0x40, 0x40, 0x40, 0x40 };
        case '/': return { 0x20, 0x10, 0x08, 0x04, 0x02 };
        case '.': return { 0x00, 0x60, 0x60, 0x00, 0x00 };
        case ':': return { 0x00, 0x36, 0x36, 0x00, 0x00 };
        case '>': return { 0x41, 0x22, 0x14, 0x08, 0x00 };
        case '<': return { 0x08, 0x14, 0x22, 0x41, 0x00 };
        default: return { 0x00, 0x00, 0x00, 0x00, 0x00 };
    }
}
} // namespace

MainComponent::OpalineLookAndFeel::OpalineLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, kValueText);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0x00000000));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x00000000));
    setColour(juce::Slider::rotarySliderFillColourId, kTeal);
    setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff5e625a));
}

void MainComponent::OpalineLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                                      const int x,
                                                      const int y,
                                                      const int width,
                                                      const int height,
                                                      const float sliderPos,
                                                      const float rotaryStartAngle,
                                                      const float rotaryEndAngle,
                                                      juce::Slider&)
{
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x),
                                              static_cast<float>(y),
                                              static_cast<float>(width),
                                              static_cast<float>(height)).reduced(3.0f);
    const auto knob = bounds.withSizeKeepingCentre(juce::jmin(bounds.getWidth(), bounds.getHeight()),
                                                   juce::jmin(bounds.getWidth(), bounds.getHeight()));
    const auto radius = knob.getWidth() * 0.46f;
    const auto centre = knob.getCentre();
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius - 2.5f, radius - 2.5f, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(juce::Colour(0xff4f554e));
    g.strokePath(track, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path active;
    active.addCentredArc(centre.x, centre.y, radius - 2.5f, radius - 2.5f, 0.0f, rotaryStartAngle, angle, true);
    g.setColour(kTeal);
    g.strokePath(active, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto outer = knob.reduced(knob.getWidth() * 0.18f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3d3c36),
                                           outer.getX(),
                                           outer.getY(),
                                           juce::Colour(0xff0b0b09),
                                           outer.getRight(),
                                           outer.getBottom(),
                                           false));
    g.fillEllipse(outer);
    g.setColour(juce::Colour(0xff070909));
    g.drawEllipse(outer, 2.0f);

    const auto inner = outer.reduced(outer.getWidth() * 0.18f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2d2c27),
                                           inner.getX(),
                                           inner.getY(),
                                           juce::Colour(0xff11110f),
                                           inner.getRight(),
                                           inner.getBottom(),
                                           false));
    g.fillEllipse(inner);
    g.setColour(juce::Colour(0xff050607));
    g.drawEllipse(inner, 1.4f);

    juce::Path pointer;
    pointer.addRoundedRectangle(-2.0f, -radius * 0.53f, 4.0f, radius * 0.42f, 1.4f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centre));
    g.setColour(kEnvelope);
    g.fillPath(pointer);
}

void MainComponent::OpalineLookAndFeel::drawLinearSlider(juce::Graphics& g,
                                                      const int x,
                                                      const int y,
                                                      const int width,
                                                      const int height,
                                                      const float sliderPos,
                                                      const float minSliderPos,
                                                      const float maxSliderPos,
                                                      const juce::Slider::SliderStyle style,
                                                      juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    auto bounds = juce::Rectangle<float>(static_cast<float>(x),
                                         static_cast<float>(y),
                                         static_cast<float>(width),
                                         static_cast<float>(height)).reduced(8.0f, 3.0f);
    if (slider.getName() == "wheelFader")
        bounds = juce::Rectangle<float>(static_cast<float>(x),
                                        static_cast<float>(y),
                                        static_cast<float>(width),
                                        static_cast<float>(height)).reduced(4.0f, 0.0f);

    if (slider.getName() == "mainFader")
    {
        bounds = juce::Rectangle<float>(static_cast<float>(x),
                                        static_cast<float>(y),
                                        static_cast<float>(width),
                                        static_cast<float>(height)).reduced(8.0f, 1.0f);
        const auto panel = bounds.reduced(1.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff08060a),
                                               panel.getX(),
                                               panel.getY(),
                                               juce::Colour(0xff010101),
                                               panel.getX(),
                                               panel.getBottom(),
                                               false));
        g.fillRoundedRectangle(panel, 3.0f);
        g.setColour(juce::Colour(0xff14110f));
        g.drawRoundedRectangle(panel, 3.0f, 1.0f);

        const float slotX = panel.getX() + panel.getWidth() * 0.32f;
        const auto slot = juce::Rectangle<float>(slotX, panel.getY() + 24.0f, 7.0f, panel.getHeight() - 48.0f);
        g.setColour(juce::Colour(0xff010101));
        g.fillRoundedRectangle(slot, 2.0f);

        const float tickX = panel.getX() + panel.getWidth() * 0.62f;
        g.setColour(juce::Colour(0xffaaa8b8).withAlpha(0.55f));
        for (int i = 0; i < 9; ++i)
        {
            const float yPos = slot.getY() + static_cast<float>(i) * slot.getHeight() / 8.0f;
            const float tickWidth = (i % 4 == 0) ? 20.0f : 15.0f;
            g.drawLine(tickX, yPos, tickX + tickWidth, yPos, 1.5f);
        }

        g.setColour(kTextPrimary);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        const auto topText = slider.getMaximum() > 1.0 ? "+24" : "MAX";
        const auto bottomText = slider.getMaximum() > 1.0 ? "-24" : "MIN";
        g.drawText(topText, panel.withTrimmedLeft(panel.getWidth() * 0.55f).withHeight(20.0f), juce::Justification::centred);
        g.drawText(bottomText,
                   panel.withTrimmedLeft(panel.getWidth() * 0.55f).withTrimmedTop(panel.getHeight() - 22.0f),
                   juce::Justification::centred);

        const double sliderRange = slider.getMaximum() - slider.getMinimum();
        const double normalizedValue = sliderRange > 0.0
            ? (slider.getValue() - slider.getMinimum()) / sliderRange
            : 0.0;
        const float valueY = juce::jmap(static_cast<float>(juce::jlimit(0.0, 1.0, normalizedValue)),
                                        slot.getBottom() - 10.0f,
                                        slot.getY() + 10.0f);
        const auto grip = juce::Rectangle<float>(panel.getX() + 2.0f, valueY - 9.0f, panel.getWidth() * 0.44f, 18.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffeef6ff),
                                               grip.getX(),
                                               grip.getY(),
                                               juce::Colour(0xff838890),
                                               grip.getX(),
                                               grip.getBottom(),
                                               false));
        g.fillRoundedRectangle(grip, 2.0f);
        g.setColour(juce::Colour(0xff252832).withAlpha(0.6f));
        g.drawRoundedRectangle(grip, 2.0f, 1.0f);
        g.setColour(juce::Colour(0xffffffff).withAlpha(0.45f));
        g.drawLine(grip.getX() + 3.0f, grip.getCentreY(), grip.getRight() - 3.0f, grip.getCentreY(), 1.0f);

        juce::String valueText;
        if (slider.getMaximum() <= 1.0)
            valueText = juce::String(static_cast<int>(std::round(slider.getValue() * 100.0)));
        else
            valueText = juce::String(static_cast<int>(std::round(slider.getValue())));

        const auto valueBox = juce::Rectangle<float>(tickX + 10.0f - 10.0f,
                                                     panel.getCentreY() - 8.0f,
                                                     20.0f,
                                                     16.0f);
        g.setColour(juce::Colour(0xff030607));
        g.fillRoundedRectangle(valueBox, 2.0f);
        g.setColour(kValueText.withAlpha(0.85f));
        g.drawRoundedRectangle(valueBox, 2.0f, 1.0f);
        g.setColour(kValueText);
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(valueText, valueBox, juce::Justification::centred);
        return;
    }

    const auto slot = bounds.withSizeKeepingCentre(juce::jmin(bounds.getWidth(), 46.0f),
                                                   bounds.getHeight());
    g.setColour(juce::Colour(0xff030303));
    g.fillRoundedRectangle(slot, 3.0f);
    g.setColour(juce::Colour(0xff24231e));
    g.drawRoundedRectangle(slot, 3.0f, 1.4f);

    const auto wheel = slider.getName() == "wheelFader" ? slot.reduced(7.0f, 2.0f)
                                                        : slot.reduced(7.0f, 8.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff10100e),
                                           wheel.getCentreX(),
                                           wheel.getY(),
                                           juce::Colour(0xff3d3c38),
                                           wheel.getCentreX(),
                                           wheel.getCentreY(),
                                           false));
    g.fillRoundedRectangle(wheel, 8.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3a3935),
                                           wheel.getCentreX(),
                                           wheel.getCentreY(),
                                           juce::Colour(0xff0b0b0a),
                                           wheel.getCentreX(),
                                           wheel.getBottom(),
                                           false));
    g.fillRoundedRectangle(wheel.withTrimmedTop(wheel.getHeight() * 0.48f), 8.0f);

    const auto minY = wheel.getBottom();
    const auto maxY = wheel.getY();
    const auto valueY = juce::jlimit(maxY + 13.0f, minY - 13.0f, sliderPos);
    const auto grip = juce::Rectangle<float>(wheel.getX() + 2.0f,
                                            valueY - 14.0f,
                                            wheel.getWidth() - 4.0f,
                                            28.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff696965),
                                           grip.getX(),
                                           grip.getY(),
                                           juce::Colour(0xff2d2d2a),
                                           grip.getX(),
                                           grip.getBottom(),
                                           false));
    g.fillRoundedRectangle(grip, 4.0f);
    g.setColour(juce::Colour(0xff11110f).withAlpha(0.7f));
    g.drawLine(grip.getX() + 2.0f, grip.getY() + 1.0f, grip.getRight() - 2.0f, grip.getY() + 1.0f, 1.0f);
    g.drawLine(grip.getX() + 2.0f, grip.getBottom() - 1.0f, grip.getRight() - 2.0f, grip.getBottom() - 1.0f, 1.0f);

    g.setColour(juce::Colour(0xff050505).withAlpha(0.72f));
    g.fillRect(wheel.withWidth(5.0f));
    g.fillRect(wheel.withX(wheel.getRight() - 5.0f).withWidth(5.0f));
    g.setColour(juce::Colour(0xff75746d).withAlpha(0.32f));
    g.drawVerticalLine(static_cast<int>(wheel.getCentreX()), wheel.getY() + 5.0f, wheel.getBottom() - 5.0f);

    g.setColour(juce::Colour(0xff050505));
    g.drawRoundedRectangle(wheel, 8.0f, 1.6f);
    g.setColour(juce::Colour(0xff000000).withAlpha(0.45f));
    g.fillRoundedRectangle(slot.reduced(1.5f).withTrimmedBottom(slot.getHeight() * 0.72f), 3.0f);
    g.fillRoundedRectangle(slot.reduced(1.5f).withTrimmedTop(slot.getHeight() * 0.72f), 3.0f);
}

void MainComponent::OpalineLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                                          juce::Button& button,
                                                          const juce::Colour& backgroundColour,
                                                          const bool shouldDrawButtonAsHighlighted,
                                                          const bool shouldDrawButtonAsDown)
{
    const bool opButton = button.getName() == "opEnableButton";
    const bool ampButton = button.getName() == "opAmpModButton";
    if (!opButton && !ampButton)
    {
        juce::LookAndFeel_V4::drawButtonBackground(g, button, backgroundColour, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
        return;
    }

    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    const bool on = button.getToggleState();
    const auto top = on ? (ampButton ? juce::Colour(0xff5ca8d7) : juce::Colour(0xff35d8bd)) : juce::Colour(0xff45443d);
    const auto bottom = on ? (ampButton ? juce::Colour(0xff22618d) : juce::Colour(0xff07876e)) : juce::Colour(0xff22221e);
    const auto outline = on ? (ampButton ? juce::Colour(0xff8bc9f0) : juce::Colour(0xff55e6d0)) : kControlBorder;

    if (shouldDrawButtonAsDown)
        bounds.translate(0.0f, 1.0f);

    g.setColour(juce::Colours::black.withAlpha(0.38f));
    g.fillRoundedRectangle(bounds.translated(0.0f, 3.0f), 4.0f);
    g.setGradientFill(juce::ColourGradient(top, bounds.getX(), bounds.getY(), bottom, bounds.getX(), bounds.getBottom(), false));
    g.fillRoundedRectangle(bounds, 4.0f);

    if (shouldDrawButtonAsHighlighted)
    {
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillRoundedRectangle(bounds.reduced(2.0f), 3.0f);
    }

    g.setColour(outline.withAlpha(on ? 0.85f : 0.6f));
    g.drawRoundedRectangle(bounds, 4.0f, 1.2f);
}

void MainComponent::OpalineLookAndFeel::drawButtonText(juce::Graphics& g,
                                                    juce::TextButton& button,
                                                    const bool,
                                                    const bool shouldDrawButtonAsDown)
{
    if (button.getName() == "voiceBankButton")
    {
        auto bounds = button.getLocalBounds().reduced(3, 1);
        if (shouldDrawButtonAsDown)
            bounds.translate(0, 1);

        g.setColour(button.findColour(juce::TextButton::textColourOffId));
        g.setFont(juce::FontOptions(12.0f, juce::Font::plain));
        g.drawFittedText(button.getButtonText(), bounds, juce::Justification::centred, 1);
        return;
    }

    if (button.getName() != "opEnableButton" && button.getName() != "opAmpModButton")
    {
        juce::LookAndFeel_V4::drawButtonText(g, button, false, shouldDrawButtonAsDown);
        return;
    }

    auto bounds = button.getLocalBounds().reduced(2);
    if (shouldDrawButtonAsDown)
        bounds.translate(0, 1);

    g.setColour(button.getToggleState() ? juce::Colours::white : kTextMuted);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawFittedText(button.getButtonText(), bounds, juce::Justification::centred, 1);
}

void MainComponent::OpalineLookAndFeel::drawToggleButton(juce::Graphics& g,
                                                      juce::ToggleButton& button,
                                                      const bool shouldDrawButtonAsHighlighted,
                                                      const bool shouldDrawButtonAsDown)
{
    if (button.getName() != "lfoSyncButton")
    {
        juce::LookAndFeel_V4::drawToggleButton(g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
        return;
    }

    constexpr float fontSize = 13.0f;
    constexpr float tickWidth = 17.0f;
    drawTickBox(g,
                button,
                4.0f,
                (static_cast<float>(button.getHeight()) - tickWidth) * 0.5f,
                tickWidth,
                tickWidth,
                button.getToggleState(),
                button.isEnabled(),
                shouldDrawButtonAsHighlighted,
                shouldDrawButtonAsDown);

    g.setColour(button.findColour(juce::ToggleButton::textColourId));
    g.setFont(juce::FontOptions(fontSize, juce::Font::plain));
    if (!button.isEnabled())
        g.setOpacity(0.5f);

    g.drawFittedText(button.getButtonText(),
                     button.getLocalBounds().withTrimmedLeft(30).withTrimmedRight(2),
                     juce::Justification::centredLeft,
                     1);
}

juce::Label* MainComponent::OpalineLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(juce::FontOptions(13.0f, juce::Font::plain));
    label->setJustificationType(juce::Justification::centred);
    return label;
}

juce::Font MainComponent::OpalineLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return juce::Font(juce::FontOptions(13.0f, juce::Font::plain));
}

juce::Font MainComponent::OpalineLookAndFeel::getPopupMenuFont()
{
    return juce::Font(juce::FontOptions(13.0f, juce::Font::plain));
}

void MainComponent::LcdComponent::setLines(juce::String topLine, juce::String bottomLine)
{
    line1 = topLine.substring(0, 16).paddedRight(' ', 16);
    line2 = bottomLine.substring(0, 16).paddedRight(' ', 16);
    repaint();
}

void MainComponent::LcdComponent::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff0b2037),
                                           area.getX(),
                                           area.getY(),
                                           juce::Colour(0xff03101f),
                                           area.getX(),
                                           area.getBottom(),
                                           false));
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(juce::Colour(0xff2462ad));
    g.drawRoundedRectangle(area, 4.0f, 1.0f);

    const auto inner = area.reduced(6.0f, 6.0f);
    constexpr int columnsPerChar = 5;
    constexpr int rowsPerChar = 8;
    constexpr int characterCount = 16;
    constexpr int lineCount = 2;

    const float pitchFromWidth = inner.getWidth() / static_cast<float>(characterCount * (columnsPerChar + 1) - 1);
    const float pitchFromHeight = inner.getHeight() / static_cast<float>(lineCount * rowsPerChar + 2);
    const float pitch = std::min(pitchFromWidth, pitchFromHeight);
    const float dot = juce::jmax(1.4f, pitch * 0.68f);
    const float matrixWidth = pitch * static_cast<float>(characterCount * (columnsPerChar + 1) - 1);
    const float matrixHeight = pitch * static_cast<float>(lineCount * rowsPerChar + 1);
    const float startX = inner.getCentreX() - matrixWidth * 0.5f;
    const float startY = inner.getCentreY() - matrixHeight * 0.5f;
    const auto offDot = juce::Colour(0xff164372).withAlpha(0.42f);
    const auto onDot = juce::Colour(0xff94e7ff);

    const auto drawLine = [&](const juce::String& text, const int lineIndex)
    {
        const float yBase = startY + static_cast<float>(lineIndex) * pitch * static_cast<float>(rowsPerChar + 1);

        for (int charIndex = 0; charIndex < characterCount; ++charIndex)
        {
            const auto glyph = lcdGlyph(text[charIndex]);
            const float xBase = startX + static_cast<float>(charIndex) * pitch * static_cast<float>(columnsPerChar + 1);

            for (int column = 0; column < columnsPerChar; ++column)
            {
                for (int row = 0; row < rowsPerChar; ++row)
                {
                    const bool enabled = (glyph[static_cast<std::size_t>(column)] & (1 << row)) != 0;
                    g.setColour(enabled ? onDot : offDot);
                    g.fillRoundedRectangle(xBase + static_cast<float>(column) * pitch,
                                           yBase + static_cast<float>(row) * pitch,
                                           dot,
                                           dot,
                                           dot * 0.22f);
                }
            }
        }
    };

    drawLine(line1, 0);
    drawLine(line2, 1);

    g.setColour(juce::Colour(0xffffffff).withAlpha(0.10f));
    g.fillRoundedRectangle(area.reduced(5.0f).withHeight(area.getHeight() * 0.24f), 3.0f);
}

void MainComponent::VoiceBadgeLabel::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);

    g.setColour(kTextPrimary);
    g.fillRoundedRectangle(bounds, 1.4f);

    g.setColour(juce::Colour(0xff11110d));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText(getText(), getLocalBounds(), juce::Justification::centred);
}

void MainComponent::AlgorithmComponent::setAlgorithm(const int newAlgorithm, const int newFeedback)
{
    algorithm = juce::jlimit(1, 8, newAlgorithm);
    feedback = juce::jlimit(0, 7, newFeedback);
    repaint();
}

void MainComponent::AlgorithmComponent::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    const auto area = bounds.withSizeKeepingCentre(juce::jmin(bounds.getWidth(), bounds.getHeight()),
                                                   juce::jmin(bounds.getWidth(), bounds.getHeight()));
    g.setGradientFill(juce::ColourGradient(kPanelTop,
                                           area.getX(),
                                           area.getY(),
                                           kPanelBottom,
                                           area.getX(),
                                           area.getBottom(),
                                           false));
    g.fillRect(area);
    g.setColour(kEnvelope);
    g.drawRect(area, 1.0f);

    const auto px = [&](const float x) { return area.getX() + area.getWidth() * x / 100.0f; };
    const auto py = [&](const float y) { return area.getY() + area.getHeight() * y / 100.0f; };
    const auto point = [&](const float x, const float y) { return juce::Point<float>(px(x), py(y)); };

    std::array<juce::Point<float>, 4> p;
    juce::Path lines;
    const auto line = [&](const float x1, const float y1, const float x2, const float y2)
    {
        lines.startNewSubPath(point(x1, y1));
        lines.lineTo(point(x2, y2));
    };

    switch (algorithm)
    {
        case 1:
            p = { point(50, 72), point(50, 56), point(50, 40), point(50, 24) };
            line(50, 18, 50, 82);
            break;
        case 2:
            p = { point(50, 70), point(50, 52), point(30, 34), point(50, 34) };
            line(50, 30, 50, 82);
            line(30, 34, 50, 52);
            break;
        case 3:
            p = { point(50, 70), point(50, 52), point(50, 34), point(70, 52) };
            line(50, 30, 50, 82);
            line(70, 52, 50, 70);
            break;
        case 4:
            p = { point(30, 70), point(30, 52), point(50, 52), point(50, 34) };
            line(30, 52, 30, 82);
            line(50, 34, 50, 52);
            line(50, 52, 30, 70);
            break;
        case 5:
            p = { point(30, 50), point(30, 31), point(50, 50), point(50, 31) };
            line(30, 31, 30, 66);
            line(30, 66, 50, 66);
            line(50, 31, 50, 66);
            break;
        case 6:
            p = { point(30, 66), point(50, 66), point(70, 66), point(50, 36) };
            line(30, 66, 30, 80);
            line(30, 80, 70, 80);
            line(50, 36, 30, 66);
            line(50, 36, 50, 80);
            line(50, 36, 70, 66);
            line(70, 66, 70, 80);
            break;
        case 7:
            p = { point(30, 58), point(50, 58), point(70, 58), point(70, 35) };
            line(30, 58, 30, 76);
            line(30, 76, 70, 76);
            line(50, 58, 50, 76);
            line(70, 35, 70, 76);
            break;
        default:
            p = { point(20, 50), point(40, 50), point(60, 50), point(80, 50) };
            line(20, 50, 20, 76);
            line(20, 76, 80, 76);
            line(40, 50, 40, 76);
            line(60, 50, 60, 76);
            line(80, 50, 80, 76);
            break;
    }

    const float boxSize = juce::jmax(8.0f, area.getWidth() * 0.145f);
    juce::Path feedbackPath;
    if (feedback > 0)
    {
        const auto op4 = p[3];
        const float loopInset = area.getWidth() * 0.14f;
        const float topY = op4.y - boxSize * 0.5f - area.getHeight() * 0.07f;
        const float rightX = op4.x + loopInset;
        feedbackPath.startNewSubPath(op4.x, op4.y + boxSize * 0.5f);
        feedbackPath.lineTo(rightX, op4.y + boxSize * 0.5f);
        feedbackPath.lineTo(rightX, topY);
        feedbackPath.lineTo(op4.x, topY);
        feedbackPath.lineTo(op4.x, op4.y - boxSize * 0.5f);
    }

    auto illustrationBounds = lines.getBounds();
    if (!feedbackPath.isEmpty())
        illustrationBounds = illustrationBounds.getUnion(feedbackPath.getBounds());
    for (const auto& pointToDraw : p)
        illustrationBounds = illustrationBounds.getUnion(juce::Rectangle<float>(boxSize, boxSize).withCentre(pointToDraw));

    const auto targetArea = area.reduced(area.getWidth() * 0.08f, area.getHeight() * 0.08f);
    const auto offset = targetArea.getCentre() - illustrationBounds.getCentre();

    g.saveState();
    g.addTransform(juce::AffineTransform::translation(offset.x, offset.y));

    g.setColour(kEnvelope);
    g.strokePath(lines, juce::PathStrokeType(1.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    if (!feedbackPath.isEmpty())
        g.strokePath(feedbackPath, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    g.setFont(juce::FontOptions(juce::jmax(6.5f, boxSize * 0.62f), juce::Font::bold));
    for (int i = 0; i < opaline::kOperatorCount; ++i)
    {
        const auto box = juce::Rectangle<float>(p[static_cast<std::size_t>(i)].x - 13.0f,
                                                p[static_cast<std::size_t>(i)].y - 10.0f,
                                                boxSize,
                                                boxSize).withCentre(p[static_cast<std::size_t>(i)]);
        g.setColour(kEnvelope);
        g.fillRect(box);
        g.setColour(juce::Colour(0xff11100d));
        g.drawText(juce::String(i + 1), box, juce::Justification::centred);
    }
    g.restoreState();

    g.setColour(kValueText);
    g.setFont(juce::FontOptions(juce::jmax(7.0f, area.getWidth() * 0.10f), juce::Font::bold));
    g.drawText(juce::String(algorithm), area.reduced(5.0f).removeFromBottom(14.0f), juce::Justification::bottomLeft);
}

MainComponent::ScopeComponent::ScopeComponent()
{
    for (auto& sample : samples)
        sample.store(0.0f, std::memory_order_relaxed);
    startTimerHz(30);
}

void MainComponent::ScopeComponent::pushSample(const float sample)
{
    const int index = writeIndex.fetch_add(1, std::memory_order_relaxed) & 255;
    samples[static_cast<std::size_t>(index)].store(juce::jlimit(-1.0f, 1.0f, sample), std::memory_order_relaxed);
}

void MainComponent::ScopeComponent::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(3.0f);
    g.setColour(kControlWell);
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(kControlBorder);
    g.drawRoundedRectangle(area, 4.0f, 1.0f);

    juce::Path path;
    const int newest = writeIndex.load(std::memory_order_relaxed);
    for (int i = 0; i < 256; ++i)
    {
        const int readIndex = (newest + i) & 255;
        const float value = samples[static_cast<std::size_t>(readIndex)].load(std::memory_order_relaxed);
        const float x = area.getX() + area.getWidth() * static_cast<float>(i) / 255.0f;
        const float y = area.getCentreY() - value * area.getHeight() * 0.42f;
        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }
    g.setColour(kTeal);
    g.strokePath(path, juce::PathStrokeType(1.5f));
}

void MainComponent::PitchEnvelopeGraphComponent::setEnvelope(const opaline::OpalinePitchEnvelopeParams& params)
{
    envelope = params;
    repaint();
}

void MainComponent::PitchEnvelopeGraphComponent::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(kControlWell);
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(kControlBorder);
    g.drawRoundedRectangle(area, 4.0f, 1.0f);

    const auto graph = area.reduced(4.0f, 3.0f);
    const float midY = graph.getCentreY();
    g.setColour(kControlBorder.withAlpha(0.75f));
    g.drawHorizontalLine(static_cast<int>(std::round(midY)), graph.getX(), graph.getRight());

    const float w1 = pegRateWeight(envelope.rate1);
    const float w2 = pegRateWeight(envelope.rate2);
    const float w3 = pegRateWeight(envelope.rate3);
    const float total = w1 + w2 + w3;
    const float left = graph.getX();
    const float right = graph.getRight();
    const float usableWidth = graph.getWidth();
    const float x0 = left;
    const float x1 = left + usableWidth * w1 / total;
    const float x2 = x1 + usableWidth * w2 / total;
    const float x3 = right;
    const auto yForLevel = [&graph](const int level)
    {
        const float normalized = juce::jlimit(-1.0f, 1.0f, pegLevelToCents(level) / 4800.0f);
        return graph.getCentreY() - normalized * graph.getHeight() * 0.46f;
    };

    juce::Path path;
    path.startNewSubPath(x0, yForLevel(envelope.level3));
    path.lineTo(x1, yForLevel(envelope.level1));
    path.lineTo(x2, yForLevel(envelope.level2));
    path.lineTo(x3, yForLevel(envelope.level3));

    g.setColour(kControlBorder);
    g.drawVerticalLine(static_cast<int>(std::round(x1)), graph.getY(), graph.getBottom());
    g.drawVerticalLine(static_cast<int>(std::round(x2)), graph.getY(), graph.getBottom());

    g.setColour(kEnvelope);
    g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setFont(juce::FontOptions(10.0f, juce::Font::plain));
    g.setColour(kTextMuted);
    g.drawText("-4", juce::Rectangle<float>(area.getX() + 2.0f, area.getBottom() - 13.0f, 22.0f, 11.0f), juce::Justification::centredLeft);
    g.drawText("+4", juce::Rectangle<float>(area.getX() + 2.0f, area.getY() + 1.0f, 22.0f, 11.0f), juce::Justification::centredLeft);
}

MainComponent::OperatorComponent::OperatorComponent(const int operatorIndex, ChangeCallback callback)
    : opIndex(operatorIndex),
      onChange(std::move(callback)),
      enableButton("OP" + juce::String(operatorIndex + 1))
{
    enableButton.setName("opEnableButton");
    enableButton.setLookAndFeel(&opalineLookAndFeel);
    addAndMakeVisible(enableButton);
    enableButton.addListener(this);

    ampModButton.setName("opAmpModButton");
    ampModButton.setLookAndFeel(&opalineLookAndFeel);
    addAndMakeVisible(ampModButton);
    ampModButton.addListener(this);

    roleLabel.setJustificationType(juce::Justification::centredRight);
    roleLabel.setFont(juce::FontOptions(13.0f, juce::Font::plain));
    roleLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(roleLabel);

    static constexpr std::array<const char*, 6> opNames { "Ratio", "Detune", "Level", "Rate", "LevelSc", "Vel" };
    static constexpr std::array<const char*, 5> egNames { "AR", "D1R", "D1L", "D2R", "RR" };
    for (int i = 0; i < 6; ++i)
        addLabeledSlider(opLabels[static_cast<std::size_t>(i)], opSliders[static_cast<std::size_t>(i)], opNames[static_cast<std::size_t>(i)]);
    for (int i = 0; i < 5; ++i)
        addLabeledSlider(egLabels[static_cast<std::size_t>(i)], egSliders[static_cast<std::size_t>(i)], egNames[static_cast<std::size_t>(i)]);

    setupSlider(opSliders[0], 0, 63, 1, op.ratioIndex);
    setupSlider(opSliders[1], -3, 3, 1, op.detune);
    setupSlider(opSliders[2], 0, 99, 1, op.level);
    setupSlider(opSliders[3], 0, 3, 1, op.rateScale);
    setupSlider(opSliders[4], 0, 99, 1, op.levelScale);
    setupSlider(opSliders[5], 0, 7, 1, op.velocity);
    setupSlider(egSliders[0], 0, 31, 1, op.envelope.attackRate);
    setupSlider(egSliders[1], 0, 31, 1, op.envelope.decay1Rate);
    setupSlider(egSliders[2], 0, 15, 1, op.envelope.decay1Level);
    setupSlider(egSliders[3], 0, 31, 1, op.envelope.decay2Rate);
    setupSlider(egSliders[4], 0, 15, 1, op.envelope.releaseRate);
}

MainComponent::OperatorComponent::~OperatorComponent()
{
    enableButton.setLookAndFeel(nullptr);
    ampModButton.setLookAndFeel(nullptr);
    for (auto& slider : opSliders)
        slider.setLookAndFeel(nullptr);
    for (auto& slider : egSliders)
        slider.setLookAndFeel(nullptr);
}

void MainComponent::OperatorComponent::setupSlider(juce::Slider& slider, const double min, const double max, const double step, const double value)
{
    slider.setRange(min, max, step);
    slider.setValue(value, juce::dontSendNotification);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 42, 16);
    slider.setLookAndFeel(&opalineLookAndFeel);
    slider.addListener(this);
}

void MainComponent::OperatorComponent::addLabeledSlider(juce::Label& label, juce::Slider& slider, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(11.0f, juce::Font::plain));
    label.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(label);
    addAndMakeVisible(slider);
}

void MainComponent::OperatorComponent::setOperator(const opaline::OpalineOperator& newOperator)
{
    op = newOperator;
    opSliders[0].setValue(op.ratioIndex, juce::dontSendNotification);
    opSliders[1].setValue(op.detune, juce::dontSendNotification);
    opSliders[2].setValue(op.level, juce::dontSendNotification);
    opSliders[3].setValue(op.rateScale, juce::dontSendNotification);
    opSliders[4].setValue(op.levelScale, juce::dontSendNotification);
    opSliders[5].setValue(op.velocity, juce::dontSendNotification);
    egSliders[0].setValue(op.envelope.attackRate, juce::dontSendNotification);
    egSliders[1].setValue(op.envelope.decay1Rate, juce::dontSendNotification);
    egSliders[2].setValue(op.envelope.decay1Level, juce::dontSendNotification);
    egSliders[3].setValue(op.envelope.decay2Rate, juce::dontSendNotification);
    egSliders[4].setValue(op.envelope.releaseRate, juce::dontSendNotification);
    enableButton.setToggleState(op.enabled, juce::dontSendNotification);
    ampModButton.setToggleState(op.ampModEnable, juce::dontSendNotification);
    updateEnableButtonStyle();
    updateAmpModButtonStyle();
    repaint();
}

void MainComponent::OperatorComponent::setRole(juce::String newRole)
{
    role = std::move(newRole);
    roleLabel.setText(role, juce::dontSendNotification);
    repaint();
}

void MainComponent::OperatorComponent::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(4.0f);
    g.setGradientFill(juce::ColourGradient(kPanelTop,
                                           area.getX(),
                                           area.getY(),
                                           kPanelBottom,
                                           area.getX(),
                                           area.getBottom(),
                                           false));
    g.fillRoundedRectangle(area, 6.0f);
    g.setColour(op.enabled ? kPanelBorderOn.withAlpha(0.58f) : kPanelBorder);
    g.drawRoundedRectangle(area, 6.0f, op.enabled ? 1.4f : 1.0f);

    auto graph = getLocalBounds().reduced(10).withTrimmedTop(36).removeFromTop(46).toFloat();
    g.setColour(kControlWell);
    g.fillRoundedRectangle(graph, 4.0f);
    const float left = graph.getX() + 8.0f;
    const float right = graph.getRight() - 8.0f;
    const float top = graph.getY() + 8.0f;
    const float bottom = graph.getBottom() - 8.0f;
    const float usableWidth = right - left;
    const float attackWeight = egTimeWeight(op.envelope.attackRate, 31);
    const float decay1Weight = egTimeWeight(op.envelope.decay1Rate, 31);
    const float decay2Weight = egTimeWeight(op.envelope.decay2Rate, 31);
    const float releaseWeight = egTimeWeight(op.envelope.releaseRate, 15);
    const float totalWeight = attackWeight + decay1Weight + decay2Weight + releaseWeight;
    const float x1 = left + usableWidth * attackWeight / totalWeight;
    const float x2 = x1 + usableWidth * decay1Weight / totalWeight;
    const float x3 = x2 + usableWidth * decay2Weight / totalWeight;
    const float sustain = static_cast<float>(juce::jlimit(0, 15, op.envelope.decay1Level)) / 15.0f;
    const float y2 = bottom - sustain * (bottom - top);
    const float y3 = op.envelope.decay2Rate > 0 ? bottom : y2;
    juce::Path path;
    path.startNewSubPath(left, bottom);
    path.lineTo(x1, top);
    path.lineTo(x2, y2);
    path.lineTo(x3, y3);
    path.lineTo(right, bottom);
    g.setColour(kControlBorder);
    g.drawVerticalLine(static_cast<int>(std::round(x1)), top, bottom);
    g.drawVerticalLine(static_cast<int>(std::round(x2)), top, bottom);
    g.drawVerticalLine(static_cast<int>(std::round(x3)), top, bottom);
    g.setColour(op.enabled ? kEnvelope : juce::Colour(0xff6f6a5b));
    g.strokePath(path, juce::PathStrokeType(2.0f));

    g.setColour(kTextMuted);
    g.setFont(juce::FontOptions(10.0f, juce::Font::plain));
    const float labelTop = graph.getY() - 14.0f;
    g.drawText("AR", juce::Rectangle<float>(left, labelTop, x1 - left, 12.0f), juce::Justification::centred);
    g.drawText("D1", juce::Rectangle<float>(x1, labelTop, x2 - x1, 12.0f), juce::Justification::centred);
    g.drawText("D2", juce::Rectangle<float>(x2, labelTop, x3 - x2, 12.0f), juce::Justification::centred);
    g.drawText("RR", juce::Rectangle<float>(x3, labelTop, right - x3, 12.0f), juce::Justification::centred);
}

void MainComponent::OperatorComponent::resized()
{
    constexpr int knobYOffset = 3;
    auto area = getLocalBounds().reduced(8);
    auto top = area.removeFromTop(38);
    const auto enableBounds = top.removeFromLeft(42).withHeight(22);
    enableButton.setBounds(enableBounds);
    top.removeFromLeft(5);
    const auto ampModBounds = top.removeFromLeft(32).withHeight(22);
    ampModButton.setBounds(ampModBounds);
    top.removeFromLeft(5);
    roleLabel.setBounds(top.withY(enableBounds.getY()).withHeight(enableBounds.getHeight()));
    area.removeFromTop(50);
    auto egRow = area.removeFromTop(74);
    const int egWidth = juce::jmax(42, egRow.getWidth() / 5);
    for (int i = 0; i < 5; ++i)
    {
        auto cell = egRow.removeFromLeft(egWidth).reduced(1);
        egLabels[static_cast<std::size_t>(i)].setBounds(cell.removeFromTop(15));
        cell.removeFromTop(knobYOffset);
        egSliders[static_cast<std::size_t>(i)].setBounds(cell);
    }
    auto opRow = area.removeFromTop(74);
    const int opWidth = juce::jmax(36, opRow.getWidth() / 6);
    for (int i = 0; i < 6; ++i)
    {
        auto cell = opRow.removeFromLeft(opWidth).reduced(1);
        opLabels[static_cast<std::size_t>(i)].setBounds(cell.removeFromTop(14));
        cell.removeFromTop(knobYOffset);
        opSliders[static_cast<std::size_t>(i)].setBounds(cell);
    }
}

void MainComponent::OperatorComponent::sliderValueChanged(juce::Slider*)
{
    op.ratioIndex = static_cast<int>(opSliders[0].getValue());
    op.detune = static_cast<int>(opSliders[1].getValue());
    op.level = static_cast<int>(opSliders[2].getValue());
    op.rateScale = static_cast<int>(opSliders[3].getValue());
    op.levelScale = static_cast<int>(opSliders[4].getValue());
    op.velocity = static_cast<int>(opSliders[5].getValue());
    op.envelope.attackRate = static_cast<int>(egSliders[0].getValue());
    op.envelope.decay1Rate = static_cast<int>(egSliders[1].getValue());
    op.envelope.decay1Level = static_cast<int>(egSliders[2].getValue());
    op.envelope.decay2Rate = static_cast<int>(egSliders[3].getValue());
    op.envelope.releaseRate = static_cast<int>(egSliders[4].getValue());
    notify();
}

void MainComponent::OperatorComponent::buttonClicked(juce::Button* button)
{
    if (button == &enableButton)
    {
        op.enabled = !op.enabled;
        enableButton.setToggleState(op.enabled, juce::dontSendNotification);
        updateEnableButtonStyle();
    }
    else if (button == &ampModButton)
    {
        op.ampModEnable = !op.ampModEnable;
        ampModButton.setToggleState(op.ampModEnable, juce::dontSendNotification);
        updateAmpModButtonStyle();
    }

    notify();
}

void MainComponent::OperatorComponent::updateEnableButtonStyle()
{
    enableButton.setButtonText("OP " + juce::String(opIndex + 1));
    enableButton.setColour(juce::TextButton::buttonColourId, op.enabled ? juce::Colour(0xff0aa878) : juce::Colour(0xff2b2a24));
    enableButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff0aa878));
    enableButton.setColour(juce::TextButton::textColourOffId, op.enabled ? juce::Colours::white : kTextMuted);
    enableButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}

void MainComponent::OperatorComponent::updateAmpModButtonStyle()
{
    ampModButton.setButtonText("AM");
    ampModButton.setColour(juce::TextButton::buttonColourId, op.ampModEnable ? juce::Colour(0xff4f8fbb) : juce::Colour(0xff2b2a24));
    ampModButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff4f8fbb));
    ampModButton.setColour(juce::TextButton::textColourOffId, op.ampModEnable ? juce::Colours::white : kTextMuted);
    ampModButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}

void MainComponent::OperatorComponent::notify()
{
    repaint();
    if (onChange)
        onChange(opIndex, op);
}

MainComponent::KeyboardComponent::KeyboardComponent(MainComponent& ownerIn)
    : owner(ownerIn)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void MainComponent::KeyboardComponent::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll(juce::Colour(0xff060707));

    const auto area = bounds.reduced(4.0f, 2.0f);
    const int whiteKeyCount = whiteKeyIndexForNote(kFirstKeyboardNote + kKeyboardNoteCount);
    const float whiteWidth = area.getWidth() / static_cast<float>(whiteKeyCount);
    const float blackWidth = whiteWidth * 0.66f;
    const float blackHeight = area.getHeight() * 0.70f;

    for (int i = 0; i < kKeyboardNoteCount; ++i)
    {
        const int note = kFirstKeyboardNote + i;
        if (isBlackKey(note))
            continue;

        const int whiteIndex = whiteKeyIndexForNote(note);
        const auto keyArea = juce::Rectangle<float>(area.getX() + whiteWidth * static_cast<float>(whiteIndex),
                                                   area.getY(),
                                                   whiteWidth - 2.0f,
                                                   area.getHeight() - 4.0f);
        const bool held = note == heldNote || owner.isMidiUiNoteHeld(note);
        g.setGradientFill(juce::ColourGradient(held ? juce::Colour(0xffa8ecff) : juce::Colour(0xfff4fbff),
                                               keyArea.getX(),
                                               keyArea.getY(),
                                               held ? juce::Colour(0xff74d6f5) : juce::Colour(0xffdde7ec),
                                               keyArea.getX(),
                                               keyArea.getBottom(),
                                               false));
        g.fillRoundedRectangle(keyArea, 5.0f);
        g.setColour(juce::Colour(0xff252b2f));
        g.drawRoundedRectangle(keyArea, 5.0f, 1.0f);
        const int velocity = note == heldNote ? 104 : owner.heldVelocityForNote(note);
        if (held && velocity > 0)
        {
            const auto valueBox = keyArea.withSizeKeepingCentre(juce::jmin(34.0f, keyArea.getWidth() - 8.0f), 18.0f)
                                    .withY(keyArea.getBottom() - 28.0f);
            g.setColour(juce::Colour(0xff050606).withAlpha(0.82f));
            g.fillRoundedRectangle(valueBox, 3.0f);
            g.setColour(kValueText);
            g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
            g.drawText(juce::String(velocity), valueBox, juce::Justification::centred);
        }
    }

    for (int i = 0; i < kKeyboardNoteCount; ++i)
    {
        const int note = kFirstKeyboardNote + i;
        if (!isBlackKey(note))
            continue;

        const int nextWhiteIndex = whiteKeyIndexForNote(note);
        const auto keyArea = juce::Rectangle<float>(area.getX() + whiteWidth * static_cast<float>(nextWhiteIndex) - blackWidth * 0.5f,
                                                   area.getY(),
                                                   blackWidth,
                                                   blackHeight);
        const bool held = note == heldNote || owner.isMidiUiNoteHeld(note);
        g.setGradientFill(juce::ColourGradient(held ? juce::Colour(0xff253f48) : juce::Colour(0xff111821),
                                               keyArea.getX(),
                                               keyArea.getY(),
                                               held ? juce::Colour(0xff0b98bd) : juce::Colour(0xff05070b),
                                               keyArea.getX(),
                                               keyArea.getBottom(),
                                               false));
        g.fillRoundedRectangle(keyArea, 4.0f);
        g.setColour(juce::Colour(0xff020304));
        g.drawRoundedRectangle(keyArea, 4.0f, 1.0f);
        g.setColour(juce::Colour(0xffffffff).withAlpha(held ? 0.18f : 0.06f));
        g.fillRoundedRectangle(keyArea.reduced(4.0f).withHeight(keyArea.getHeight() * 0.22f), 3.0f);
        const int velocity = note == heldNote ? 104 : owner.heldVelocityForNote(note);
        if (held && velocity > 0)
        {
            const auto valueBox = keyArea.withSizeKeepingCentre(juce::jmin(32.0f, keyArea.getWidth() - 6.0f), 17.0f)
                                    .withY(keyArea.getBottom() - 24.0f);
            g.setColour(juce::Colour(0xff050606).withAlpha(0.84f));
            g.fillRoundedRectangle(valueBox, 3.0f);
            g.setColour(kValueText);
            g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
            g.drawText(juce::String(velocity), valueBox, juce::Justification::centred);
        }
    }

    g.setColour(juce::Colour(0xff22251f));
    g.drawRoundedRectangle(bounds.reduced(1.0f), 4.0f, 1.0f);
}

int MainComponent::KeyboardComponent::noteForPosition(const juce::Point<int> position) const
{
    const auto area = getLocalBounds().toFloat().reduced(4.0f, 2.0f);
    const int whiteKeyCount = whiteKeyIndexForNote(kFirstKeyboardNote + kKeyboardNoteCount);
    const float whiteWidth = area.getWidth() / static_cast<float>(whiteKeyCount);
    const float blackWidth = whiteWidth * 0.66f;
    const float blackHeight = area.getHeight() * 0.70f;
    const auto point = position.toFloat();

    if (point.y <= area.getY() + blackHeight)
    {
        for (int i = 0; i < kKeyboardNoteCount; ++i)
        {
            const int note = kFirstKeyboardNote + i;
            if (!isBlackKey(note))
                continue;

            const int nextWhiteIndex = whiteKeyIndexForNote(note);
            const auto keyArea = juce::Rectangle<float>(area.getX() + whiteWidth * static_cast<float>(nextWhiteIndex) - blackWidth * 0.5f,
                                                       area.getY(),
                                                       blackWidth,
                                                       blackHeight);
            if (keyArea.contains(point))
                return note;
        }
    }

    const int whiteIndex = juce::jlimit(0,
                                       whiteKeyCount - 1,
                                       static_cast<int>((point.x - area.getX()) / whiteWidth));
    return noteForWhiteKeyIndex(whiteIndex);
}

void MainComponent::KeyboardComponent::updateHeldNote(const int note)
{
    if (heldNote == note)
        return;

    if (heldNote >= 0)
        owner.noteOff(heldNote);

    heldNote = note;
    if (heldNote >= 0)
        owner.noteOn(heldNote, 104);

    repaint();
}

void MainComponent::KeyboardComponent::mouseDown(const juce::MouseEvent& event)
{
    updateHeldNote(noteForPosition(event.getPosition()));
}

void MainComponent::KeyboardComponent::mouseDrag(const juce::MouseEvent& event)
{
    updateHeldNote(noteForPosition(event.getPosition()));
}

void MainComponent::KeyboardComponent::mouseUp(const juce::MouseEvent&)
{
    updateHeldNote(-1);
}

void MainComponent::KeyboardComponent::mouseExit(const juce::MouseEvent&)
{
    updateHeldNote(-1);
}

MainComponent::MainComponent(const HostMode mode)
    : keyboard(*this),
      hostMode(mode)
{
    setupLabel(titleLabel, "Opaline FM");
    titleLabel.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(statusLabel);
    setupLabel(voiceALabel, "A");
    voiceALabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(voiceALabel);
    setupComboBox(voiceBankSelect);
    voiceBankSelect.addListener(this);
    addAndMakeVisible(voiceBankSelect);
    for (auto* button : { &loadVoiceBankButton, &saveVoiceBankButton, &exportVoiceLibraryButton, &storeVoiceButton })
    {
        button->setName("voiceBankButton");
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1c1a15));
        button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff1c1a15));
        button->setColour(juce::TextButton::textColourOffId, kTextPrimary);
        button->setColour(juce::TextButton::textColourOnId, kTextPrimary);
        button->setLookAndFeel(&opalineLookAndFeel);
        button->addListener(this);
        addAndMakeVisible(*button);
    }
    setupComboBox(voiceSelect);
    voiceSelect.addListener(this);
    addAndMakeVisible(voiceSelect);

    performanceModeSelect.addItem("SINGLE", 1);
    performanceModeSelect.addItem("DUAL", 2);
    performanceModeSelect.addItem("SPLIT", 3);
    setupComboBox(performanceModeSelect);
    performanceModeSelect.setSelectedId(1, juce::dontSendNotification);
    performanceModeSelect.addListener(this);
    addAndMakeVisible(performanceModeSelect);

    setupLabel(voiceBLabel, "B");
    voiceBLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(voiceBLabel);
    setupComboBox(voiceBSelect);
    voiceBSelect.addListener(this);
    addAndMakeVisible(voiceBSelect);

    setupLabel(dualDetuneLabel, "Detune");
    dualDetuneLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(dualDetuneLabel);
    setupSlider(dualDetuneSlider, -16, 16, 1, 0, juce::Slider::LinearHorizontal);
    dualDetuneSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 32, 16);
    dualDetuneSlider.addListener(this);
    addAndMakeVisible(dualDetuneSlider);

    setupLabel(splitPointLabel, "Split");
    splitPointLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(splitPointLabel);
    setupSlider(splitPointSlider, 0, 127, 1, 60, juce::Slider::LinearHorizontal);
    splitPointSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 38, 16);
    splitPointSlider.setScrollWheelEnabled(true);
    splitPointSlider.setValue(60, juce::dontSendNotification);
    splitPointSlider.addListener(this);
    addAndMakeVisible(splitPointSlider);

    setupComboBox(audioOutputSelect);
    audioOutputSelect.addListener(this);
    addAndMakeVisible(audioOutputSelect);

    setupComboBox(midiInputSelect);
    midiInputSelect.addListener(this);
    addAndMakeVisible(midiInputSelect);

    powerButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff172b27));
    powerButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff0aa878));
    powerButton.setColour(juce::TextButton::textColourOffId, kTextPrimary);
    powerButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    powerButton.addListener(this);
    addAndMakeVisible(powerButton);

    engineModelButton.setClickingTogglesState(true);
    engineModelButton.setName("voiceBankButton");
    engineModelButton.addListener(this);
    refreshEngineModelButton();
    addAndMakeVisible(engineModelButton);

    setupLabel(volumeLabel, "Volume");
    volumeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(volumeLabel);
    setupSlider(volumeSlider, 0.0, 1.0, 0.01, masterVolume, juce::Slider::LinearVertical);
    volumeSlider.setName("mainFader");
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.setLookAndFeel(&opalineLookAndFeel);
    volumeSlider.addListener(this);
    addAndMakeVisible(volumeSlider);

    setupLabel(transposeLabel, "Transpose");
    transposeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(transposeLabel);
    setupSlider(transposeSlider, -24.0, 24.0, 1.0, 0.0, juce::Slider::LinearVertical);
    transposeSlider.setName("mainFader");
    transposeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    transposeSlider.setLookAndFeel(&opalineLookAndFeel);
    transposeSlider.addListener(this);
    addAndMakeVisible(transposeSlider);

    setupLabel(algorithmLabel, "ALG");
    setupSlider(algorithmSlider, 1, 8, 1, 1, juce::Slider::RotaryHorizontalVerticalDrag);
    algorithmSlider.addListener(this);
    addAndMakeVisible(algorithmLabel);
    addAndMakeVisible(algorithmSlider);

    setupLabel(feedbackLabel, "FB");
    setupSlider(feedbackSlider, 0, 7, 1, 2, juce::Slider::RotaryHorizontalVerticalDrag);
    feedbackSlider.addListener(this);
    addAndMakeVisible(feedbackLabel);
    addAndMakeVisible(feedbackSlider);
    addAndMakeVisible(algorithmView);
    addAndMakeVisible(lcd);
    addAndMakeVisible(scope);

    setupLabel(lfoWaveLabel, "LFO Wave");
    lfoWaveSelect.addItem("SAW UP", 1);
    lfoWaveSelect.addItem("SQUARE", 2);
    lfoWaveSelect.addItem("TRIANGLE", 3);
    lfoWaveSelect.addItem("S/H", 4);
    setupComboBox(lfoWaveSelect);
    lfoWaveSelect.addListener(this);
    addAndMakeVisible(lfoWaveLabel);
    addAndMakeVisible(lfoWaveSelect);
    lfoSyncButton.setName("lfoSyncButton");
    lfoSyncButton.setLookAndFeel(&opalineLookAndFeel);
    lfoSyncButton.setColour(juce::ToggleButton::textColourId, kTextMuted);
    lfoSyncButton.setColour(juce::ToggleButton::tickColourId, kTeal);
    lfoSyncButton.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff555044));
    lfoSyncButton.addListener(this);
    addAndMakeVisible(lfoSyncButton);
    pegLeftSeparator.setColour(juce::Label::backgroundColourId, kPanelBorder.withAlpha(0.9f));
    lfoLeftSeparator.setColour(juce::Label::backgroundColourId, kPanelBorder.withAlpha(0.9f));
    lfoRightSeparator.setColour(juce::Label::backgroundColourId, kPanelBorder.withAlpha(0.9f));
    addAndMakeVisible(pegLeftSeparator);
    addAndMakeVisible(lfoLeftSeparator);
    addAndMakeVisible(lfoRightSeparator);

    setupLabel(lfoSpeedLabel, "Speed");
    setupLabel(lfoDelayLabel, "Delay");
    setupLabel(lfoPitchDepthLabel, "PMD");
    setupLabel(lfoAmpDepthLabel, "AMD");
    setupLabel(lfoPitchSensitivityLabel, "PMS");
    setupLabel(lfoAmpSensitivityLabel, "AMS");
    setupSlider(lfoSpeedSlider, 0, 99, 1, 24, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(lfoDelaySlider, 0, 99, 1, 0, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(lfoPitchDepthSlider, 0, 99, 1, 0, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(lfoAmpDepthSlider, 0, 99, 1, 0, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(lfoPitchSensitivitySlider, 0, 7, 1, 3, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(lfoAmpSensitivitySlider, 0, 3, 1, 0, juce::Slider::RotaryHorizontalVerticalDrag);
    for (auto* slider : { &lfoSpeedSlider, &lfoDelaySlider, &lfoPitchDepthSlider, &lfoAmpDepthSlider,
                          &lfoPitchSensitivitySlider, &lfoAmpSensitivitySlider })
    {
        slider->addListener(this);
        addAndMakeVisible(*slider);
    }
    for (auto* label : { &lfoSpeedLabel, &lfoDelayLabel, &lfoPitchDepthLabel, &lfoAmpDepthLabel,
                         &lfoPitchSensitivityLabel, &lfoAmpSensitivityLabel })
        addAndMakeVisible(*label);

    setupLabel(pegTitleLabel, "Pitch EG");
    pegTitleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(pegTitleLabel);
    addAndMakeVisible(pegGraph);
    setupLabel(pegRate1Label, "PR1");
    setupLabel(pegRate2Label, "PR2");
    setupLabel(pegRate3Label, "PR3");
    setupLabel(pegLevel1Label, "PL1");
    setupLabel(pegLevel2Label, "PL2");
    setupLabel(pegLevel3Label, "PL3");
    setupSlider(pegRate1Slider, 0, 99, 1, 99, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(pegRate2Slider, 0, 99, 1, 99, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(pegRate3Slider, 0, 99, 1, 99, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(pegLevel1Slider, 0, 99, 1, 50, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(pegLevel2Slider, 0, 99, 1, 50, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(pegLevel3Slider, 0, 99, 1, 50, juce::Slider::RotaryHorizontalVerticalDrag);
    for (auto* slider : { &pegRate1Slider, &pegRate2Slider, &pegRate3Slider,
                          &pegLevel1Slider, &pegLevel2Slider, &pegLevel3Slider })
    {
        slider->addListener(this);
        addAndMakeVisible(*slider);
    }
    for (auto* label : { &pegRate1Label, &pegRate2Label, &pegRate3Label,
                         &pegLevel1Label, &pegLevel2Label, &pegLevel3Label })
        addAndMakeVisible(*label);

    setupLabel(effectReverbLabel, "Reverb");
    setupLabel(effectMixLabel, "Mix");
    setupLabel(effectToneLabel, "Tone");
    setupLabel(effectChorusLabel, "Chorus");
    setupLabel(effectDelayLabel, "Delay");
    setupSlider(effectReverbSlider, 0, 99, 1, 0, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(effectMixSlider, 0, 99, 1, 0, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(effectToneSlider, 0, 99, 1, 50, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(effectChorusSlider, 0, 99, 1, 0, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(effectDelaySlider, 0, 99, 1, 0, juce::Slider::RotaryHorizontalVerticalDrag);
    for (auto* slider : { &effectReverbSlider, &effectMixSlider, &effectToneSlider, &effectChorusSlider, &effectDelaySlider })
    {
        slider->addListener(this);
        addAndMakeVisible(*slider);
    }
    for (auto* label : { &effectReverbLabel, &effectMixLabel, &effectToneLabel, &effectChorusLabel, &effectDelayLabel })
        addAndMakeVisible(*label);

    setupLabel(pitchWheelLabel, "Pitch");
    pitchWheelLabel.setJustificationType(juce::Justification::centred);
    setupSlider(pitchWheelSlider, -1.0, 1.0, 0.01, 0.0, juce::Slider::LinearVertical);
    pitchWheelSlider.setName("wheelFader");
    pitchWheelSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    pitchWheelSlider.setScrollWheelEnabled(false);
    pitchWheelSlider.setDoubleClickReturnValue(true, 0.0);
    pitchWheelSlider.setLookAndFeel(&opalineLookAndFeel);
    pitchWheelSlider.addListener(this);
    addAndMakeVisible(pitchWheelLabel);
    addAndMakeVisible(pitchWheelSlider);
    setupLabel(modWheelLabel, "Mod");
    modWheelLabel.setJustificationType(juce::Justification::centred);
    setupSlider(modWheelSlider, 0.0, 1.0, 0.01, 0.0, juce::Slider::LinearVertical);
    modWheelSlider.setName("wheelFader");
    modWheelSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    modWheelSlider.setScrollWheelEnabled(false);
    modWheelSlider.setDoubleClickReturnValue(true, 0.0);
    modWheelSlider.setLookAndFeel(&opalineLookAndFeel);
    modWheelSlider.addListener(this);
    addAndMakeVisible(modWheelLabel);
    addAndMakeVisible(modWheelSlider);

    addAndMakeVisible(keyboard);

    for (int i = 0; i < opaline::kOperatorCount; ++i)
    {
        operatorPanels[static_cast<std::size_t>(i)] = std::make_unique<OperatorComponent>(
            i,
            [this](const int index, const opaline::OpalineOperator& op)
            {
                currentPatch.operators[static_cast<std::size_t>(index)] = op;
                applyPatchToEngine();
            });
        addAndMakeVisible(*operatorPanels[static_cast<std::size_t>(i)]);
    }

    loadFactoryVoices();
    refreshPerformanceControls();
    applySelectedVoice();
    if (hostMode == HostMode::StandaloneApp)
    {
        populateAudioOutputSelect();
        populateMidiInputSelect();
        midiStatus = "MIDI: off";
    }
    else
    {
        audioOutputSelect.addItem("Audio: Host", 1);
        audioOutputSelect.setSelectedId(1, juce::dontSendNotification);
        audioOutputSelect.setEnabled(false);
        midiInputSelect.addItem("MIDI: Host", 1);
        midiInputSelect.setSelectedId(1, juce::dontSendNotification);
        midiInputSelect.setEnabled(false);
        powerOn = true;
        powerButton.setButtonText("HOST");
        powerButton.setToggleState(true, juce::dontSendNotification);
        powerButton.setEnabled(false);
        powerButton.setVisible(false);
        audioOutputSelect.setVisible(false);
        midiInputSelect.setVisible(false);
        audioStatus = "Audio: host";
        midiStatus = "MIDI: host";
    }
    refreshStatus();

    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    startTimerHz(60);

    setSize(1024, 760);
}

MainComponent::~MainComponent()
{
    if (hostMode == HostMode::StandaloneApp)
        saveVoiceLibraryState();
    stopTimer();
    for (auto& input : midiInputs)
    {
        if (input)
            input->stop();
    }
    midiInputs.clear();

    const std::array<juce::Slider*, 25> sliders { &volumeSlider, &transposeSlider, &algorithmSlider, &feedbackSlider,
                                                  &lfoSpeedSlider, &lfoDelaySlider, &lfoPitchDepthSlider, &lfoAmpDepthSlider,
                                                  &lfoPitchSensitivitySlider, &lfoAmpSensitivitySlider,
                                                  &pegRate1Slider, &pegRate2Slider, &pegRate3Slider,
                                                  &pegLevel1Slider, &pegLevel2Slider, &pegLevel3Slider,
                                                  &effectReverbSlider, &effectMixSlider, &effectToneSlider, &effectChorusSlider,
                                                  &effectDelaySlider, &pitchWheelSlider, &modWheelSlider,
                                                  &dualDetuneSlider, &splitPointSlider };
    for (auto* slider : sliders)
    {
        slider->setLookAndFeel(nullptr);
    }

    for (auto* comboBox : { &voiceSelect, &voiceBankSelect, &performanceModeSelect, &voiceBSelect,
                            &audioOutputSelect, &midiInputSelect, &lfoWaveSelect })
        comboBox->setLookAndFeel(nullptr);
    for (auto* button : { &loadVoiceBankButton, &saveVoiceBankButton, &exportVoiceLibraryButton, &storeVoiceButton })
        button->setLookAndFeel(nullptr);
    lfoSyncButton.setLookAndFeel(nullptr);

    if (audioStarted)
    {
        audioSourcePlayer.setSource(nullptr);
        if (audioDeviceManager)
        {
            audioDeviceManager->removeAudioCallback(&audioSourcePlayer);
            audioDeviceManager->closeAudioDevice();
        }
    }
}

void MainComponent::setStateChangedCallback(StateChangedCallback callback)
{
    onStateChanged = std::move(callback);
}

void MainComponent::setRenderModelChangedCallback(RenderModelChangedCallback callback)
{
    onRenderModelChanged = std::move(callback);
}

void MainComponent::setNoteOnCallback(NoteOnCallback callback)
{
    onNoteOn = std::move(callback);
}

void MainComponent::setNoteOffCallback(NoteOffCallback callback)
{
    onNoteOff = std::move(callback);
}

void MainComponent::setAllNotesOffCallback(AllNotesOffCallback callback)
{
    onAllNotesOff = std::move(callback);
}

void MainComponent::setPitchBendCallback(ControllerCallback callback)
{
    onPitchBend = std::move(callback);
}

void MainComponent::setModWheelCallback(ControllerCallback callback)
{
    onModWheel = std::move(callback);
}

void MainComponent::setProgramNameChangedCallback(ProgramNameChangedCallback callback)
{
    onProgramNameChanged = std::move(callback);
}

juce::String MainComponent::currentProgramName() const
{
    return performanceVoiceText(performanceState.voiceAIndex);
}

void MainComponent::setExternalMidiNoteState(const std::array<int, 128>& velocities)
{
    bool changed = false;
    for (std::size_t i = 0; i < velocities.size(); ++i)
    {
        const int velocity = juce::jlimit(0, 127, velocities[i]);
        const bool held = velocity > 0;
        if (gMidiUiHeldNotes[i].load(std::memory_order_relaxed) != held
            || gMidiUiHeldVelocities[i].load(std::memory_order_relaxed) != velocity)
        {
            changed = true;
        }

        gMidiUiHeldNotes[i].store(held, std::memory_order_relaxed);
        gMidiUiHeldVelocities[i].store(velocity, std::memory_order_relaxed);
    }

    if (changed)
        repaintKeyboardAsync();
}

void MainComponent::prepareToPlay(int, const double sampleRate)
{
    std::lock_guard<std::mutex> lock(engineMutex);
    audioSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    engine.prepare(audioSampleRate, performanceState.mode == PerformanceMode::Single ? 8 : 4);
    performanceEngineB.prepare(audioSampleRate, 4);
    applyRenderModelToEnginesNoLock();
    engine.setPatch(currentPatch);
    performanceEngineB.setPatch(patchForVoiceIndex(performanceState.voiceBIndex));
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferToFill.clearActiveBufferRegion();
    if (!powerOn)
        return;

    auto* left = bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);
    auto* right = bufferToFill.buffer->getNumChannels() > 1
        ? bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample)
        : left;

    float outputTrim = 1.0f;
    if (audioDeviceManager != nullptr)
    {
        if (auto* device = audioDeviceManager->getCurrentAudioDevice())
        {
            if (device->getTypeName() == "ASIO")
                outputTrim = kAsioOutputTrim;
        }
    }

    const float outputGain = masterVolume * outputTrim;

    std::lock_guard<std::mutex> lock(engineMutex);
    for (int i = 0; i < bufferToFill.numSamples; ++i)
    {
        const auto sampleA = engine.renderSample();
        auto sample = sampleA;
        if (performanceState.mode != PerformanceMode::Single)
        {
            const auto sampleB = performanceEngineB.renderSample();
            const float mixGain = performanceState.mode == PerformanceMode::Dual ? 0.72f : 1.0f;
            sample.left = (sampleA.left + sampleB.left) * mixGain;
            sample.right = (sampleA.right + sampleB.right) * mixGain;
        }

        left[i] = sample.left * outputGain;
        right[i] = sample.right * outputGain;
        if ((i & 7) == 0)
            scope.pushSample(left[i]);
    }
}

void MainComponent::releaseResources() {}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(kAppBackground);
    auto area = getLocalBounds().reduced(12).toFloat();
    g.setGradientFill(juce::ColourGradient(kPanelTop,
                                           area.getX(),
                                           area.getY(),
                                           kPanelBottom,
                                           area.getX(),
                                           area.getBottom(),
                                           false));
    g.fillRoundedRectangle(area, 6.0f);
    g.setColour(kPanelBorder);
    g.drawRoundedRectangle(area, 6.0f, 1.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(20);
    auto header = area.removeFromTop(34);
    titleLabel.setBounds(header.removeFromLeft(210));
    if (hostMode == HostMode::StandaloneApp)
    {
        powerButton.setBounds(header.removeFromRight(82));
        header.removeFromRight(10);
    }
    engineModelButton.setBounds(header.removeFromRight(64));
    header.removeFromRight(8);
    if (hostMode == HostMode::StandaloneApp)
    {
        midiInputSelect.setBounds(header.removeFromRight(180));
        header.removeFromRight(8);
        audioOutputSelect.setBounds(header.removeFromRight(235));
        header.removeFromRight(12);
    }
    statusLabel.setBounds(header);

    area.removeFromTop(4);
    auto top = area.removeFromTop(220);
    auto faders = top.removeFromLeft(150).reduced(4, 0);
    auto volumeArea = faders.removeFromLeft(70);
    volumeSlider.setBounds(volumeArea.removeFromTop(180));
    volumeLabel.setBounds(volumeArea.removeFromTop(18).translated(0, -6));
    faders.removeFromLeft(6);
    auto transposeArea = faders.removeFromLeft(70);
    transposeSlider.setBounds(transposeArea.removeFromTop(180));
    transposeLabel.setBounds(transposeArea.removeFromTop(18).translated(0, -6));

    auto patch = top.removeFromLeft(300).reduced(4);
    auto bankRow = patch.removeFromTop(28);
    voiceBankSelect.setBounds(bankRow.removeFromLeft(112));
    bankRow.removeFromLeft(3);
    loadVoiceBankButton.setBounds(bankRow.removeFromLeft(48).withSizeKeepingCentre(48, 22));
    bankRow.removeFromLeft(3);
    saveVoiceBankButton.setBounds(bankRow.removeFromLeft(48).withSizeKeepingCentre(48, 22));
    bankRow.removeFromLeft(3);
    exportVoiceLibraryButton.setBounds(bankRow.removeFromLeft(58).withSizeKeepingCentre(58, 22));
    patch.removeFromTop(4);
    lcd.setBounds(patch.removeFromTop(58));
    patch.removeFromTop(1);
    scope.setBounds(patch.removeFromTop(34));
    patch.removeFromTop(1);
    auto aRow = patch.removeFromTop(28);
    voiceALabel.setBounds(aRow.removeFromLeft(22).withSizeKeepingCentre(20, 20));
    aRow.removeFromLeft(2);
    storeVoiceButton.setBounds(aRow.removeFromRight(50).withSizeKeepingCentre(50, 22));
    aRow.removeFromRight(3);
    voiceSelect.setBounds(aRow);
    patch.removeFromTop(2);
    auto perfRow = patch.removeFromTop(26);
    performanceModeSelect.setBounds(perfRow.removeFromLeft(88));
    perfRow.removeFromLeft(4);
    dualDetuneLabel.setBounds(perfRow.removeFromLeft(48));
    dualDetuneSlider.setBounds(perfRow);
    splitPointLabel.setBounds(dualDetuneLabel.getBounds());
    splitPointSlider.setBounds(dualDetuneSlider.getBounds());
    patch.removeFromTop(2);
    auto bRow = patch.removeFromTop(26);
    voiceBLabel.setBounds(bRow.removeFromLeft(22).withSizeKeepingCentre(20, 20));
    bRow.removeFromLeft(2);
    voiceBSelect.setBounds(bRow);

    constexpr int knobSize = 54;
    constexpr int knobLabelHeight = 15;
    constexpr int knobCellWidth = 32;
    constexpr int knobCellHeight = 80;
    auto controlsPanel = top.reduced(4, 0);
    const auto controlsPanelBounds = controlsPanel;
    constexpr int voicePanelWidth = 150;
    constexpr int minimumKnobGroupWidth = 118;
    const int sharedKnobGroupWidth = juce::jmax(minimumKnobGroupWidth,
                                                (controlsPanel.getWidth() - voicePanelWidth) / 3);
    auto voice = controlsPanel.removeFromLeft(voicePanelWidth);
    auto peg = controlsPanel.removeFromLeft(juce::jmin(sharedKnobGroupWidth, controlsPanel.getWidth()));
    auto lfo = controlsPanel.removeFromLeft(juce::jmin(sharedKnobGroupWidth, controlsPanel.getWidth()));
    auto effects = controlsPanel;

    auto lfoTop = juce::Rectangle<int>(lfo.getX() + 8,
                                       controlsPanelBounds.getY(),
                                       juce::jmin(254, controlsPanelBounds.getRight() - lfo.getX() - 8),
                                       28);
    lfoWaveLabel.setBounds(lfoTop.removeFromLeft(58));
    lfoWaveSelect.setBounds(lfoTop.removeFromLeft(112));
    lfoTop.removeFromLeft(8);
    lfoSyncButton.setBounds(lfoTop.removeFromLeft(76));

    const int knobRow1Y = controlsPanelBounds.getY() + 36;
    const int knobRow2Y = knobRow1Y + knobCellHeight;
    const auto layoutKnob = [](juce::Rectangle<int> cell, juce::Label& label, juce::Slider& slider)
    {
        label.setJustificationType(juce::Justification::centred);
        label.setBounds(cell.removeFromTop(knobLabelHeight));
        auto knobArea = cell.removeFromTop(knobSize);
        slider.setBounds(knobArea.withSizeKeepingCentre(knobSize, knobSize));
    };

    pegLeftSeparator.setBounds(peg.getX(), knobRow1Y - 8, 1, peg.getBottom() - (knobRow1Y - 8));
    lfoLeftSeparator.setBounds(lfo.getX(), knobRow1Y - 8, 1, lfo.getBottom() - (knobRow1Y - 8));
    lfoRightSeparator.setBounds(lfo.getRight() - 1, knobRow1Y - 8, 1, lfo.getBottom() - (knobRow1Y - 8));

    auto voiceContent = voice.reduced(0, 0);
    algorithmLabel.setBounds(voiceContent.getX(), voiceContent.getY(), voiceContent.getWidth(), 22);
    algorithmView.setBounds(voiceContent.getX(), voiceContent.getY() + 28, 84, 84);
    pegGraph.setBounds(voiceContent.getX(), voiceContent.getY() + 124, 84, 64);
    auto pegContent = peg.reduced(0, 0);
    const int pegTopY = knobRow1Y;
    const int pegCellWidth = juce::jmax(32, pegContent.getWidth() / 3);
    const int pegCellHeight = knobCellHeight;
    const int voiceKnobX = pegContent.getX() - pegCellWidth;
    layoutKnob(juce::Rectangle<int>(voiceKnobX, knobRow1Y, pegCellWidth, knobCellHeight).reduced(1),
               algorithmLabel,
               algorithmSlider);
    layoutKnob(juce::Rectangle<int>(voiceKnobX, knobRow2Y, pegCellWidth, knobCellHeight).reduced(1),
               feedbackLabel,
               feedbackSlider);

    pegTitleLabel.setBounds(pegContent.getX() + 3, controlsPanelBounds.getY(), pegContent.getWidth() - 6, 24);
    std::array<juce::Label*, 6> pegLabels { &pegRate1Label, &pegRate2Label, &pegRate3Label,
                                            &pegLevel1Label, &pegLevel2Label, &pegLevel3Label };
    std::array<juce::Slider*, 6> pegSliders { &pegRate1Slider, &pegRate2Slider, &pegRate3Slider,
                                              &pegLevel1Slider, &pegLevel2Slider, &pegLevel3Slider };
    for (int row = 0; row < 2; ++row)
    {
        for (int col = 0; col < 3; ++col)
        {
            const int index = row * 3 + col;
            auto cell = juce::Rectangle<int>(pegContent.getX() + col * pegCellWidth,
                                             pegTopY + row * pegCellHeight,
                                             pegCellWidth,
                                             pegCellHeight).reduced(1);
            layoutKnob(cell, *pegLabels[static_cast<std::size_t>(index)], *pegSliders[static_cast<std::size_t>(index)]);
        }
    }

    auto lfoContent = lfo.reduced(0, 0);
    const int lfoCellWidth = juce::jmax(knobCellWidth, lfoContent.getWidth() / 3);
    std::array<juce::Label*, 6> lfoLabels { &lfoSpeedLabel, &lfoDelayLabel, &lfoPitchDepthLabel, &lfoAmpDepthLabel, &lfoPitchSensitivityLabel, &lfoAmpSensitivityLabel };
    std::array<juce::Slider*, 6> lfoSliders { &lfoSpeedSlider, &lfoDelaySlider, &lfoPitchDepthSlider, &lfoAmpDepthSlider, &lfoPitchSensitivitySlider, &lfoAmpSensitivitySlider };
    for (int row = 0; row < 2; ++row)
    {
        for (int col = 0; col < 3; ++col)
        {
            const int index = row * 3 + col;
            auto cell = juce::Rectangle<int>(lfoContent.getX() + col * lfoCellWidth,
                                             row == 0 ? knobRow1Y : knobRow2Y,
                                             lfoCellWidth,
                                             knobCellHeight).reduced(1);
            layoutKnob(cell, *lfoLabels[static_cast<std::size_t>(index)], *lfoSliders[static_cast<std::size_t>(index)]);
        }
    }

    auto effectsContent = effects.reduced(0, 0);
    const int effectsCellWidth = juce::jmax(knobCellWidth, effectsContent.getWidth() / 3);
    std::array<juce::Label*, 5> effectLabels { &effectReverbLabel, &effectMixLabel, &effectToneLabel, &effectChorusLabel, &effectDelayLabel };
    std::array<juce::Slider*, 5> effectSliders { &effectReverbSlider, &effectMixSlider, &effectToneSlider, &effectChorusSlider, &effectDelaySlider };
    for (int row = 0; row < 2; ++row)
    {
        for (int col = 0; col < 3; ++col)
        {
            const int index = row * 3 + col;
            if (index >= static_cast<int>(effectLabels.size()))
                continue;

            auto cell = juce::Rectangle<int>(effectsContent.getX() + col * effectsCellWidth,
                                             row == 0 ? knobRow1Y : knobRow2Y,
                                             effectsCellWidth,
                                             knobCellHeight).reduced(1);
            layoutKnob(cell, *effectLabels[static_cast<std::size_t>(index)], *effectSliders[static_cast<std::size_t>(index)]);
        }
    }

    area.removeFromTop(0);
    auto middle = area.removeFromTop(151);
    auto pitchArea = middle.removeFromLeft(58).reduced(2, 0);
    pitchWheelLabel.setBounds(pitchArea.removeFromTop(18));
    pitchWheelSlider.setBounds(pitchArea);
    auto modArea = middle.removeFromLeft(58).reduced(2, 0);
    modWheelLabel.setBounds(modArea.removeFromTop(18));
    modWheelSlider.setBounds(modArea);
    middle.removeFromLeft(4);
    keyboard.setBounds(middle.reduced(2));

    area.removeFromTop(10);
    auto ops = area;
    const int panelWidth = ops.getWidth() / opaline::kOperatorCount;
    const int panelHeight = juce::jmin(ops.getHeight(), 272);
    for (int i = 0; i < opaline::kOperatorCount; ++i)
    {
        operatorPanels[static_cast<std::size_t>(i)]->setBounds(ops.getX() + i * panelWidth,
                                                               ops.getY(),
                                                               panelWidth - 8,
                                                               panelHeight);
    }
}

void MainComponent::loadFactoryVoices()
{
    voiceLibrary = opaline::makeInitVoiceLibrary();

#ifdef OPALINE_ASSET_DIR
    const auto syxFile = juce::File(juce::String(OPALINE_ASSET_DIR)).getChildFile("factory.syx");
    const auto bytes = readBinaryFile(syxFile);
    if (!bytes.empty())
    {
        try
        {
            voiceLibrary.banks[0] = opaline::voiceBankFromSysex(bytes, "Factory");
        }
        catch (const std::exception&)
        {
            voiceLibrary.banks[0] = opaline::makeInitVoiceBank("Factory");
        }
    }
#endif

    currentVoiceBankIndex = 0;
    restoreSavedVoiceLibraryState();
    populateVoiceBankSelect();
    refreshVoiceLists();
}

void MainComponent::populateVoiceBankSelect()
{
    voiceBankSelect.clear(juce::dontSendNotification);
    for (int i = 0; i < opaline::kOpalineVoiceBankCount; ++i)
    {
        const auto& bank = voiceLibrary.banks[static_cast<std::size_t>(i)];
        const auto name = bank.name.empty() ? juce::String("Bank ") + juce::String(i + 1) : juce::String(bank.name);
        voiceBankSelect.addItem(juce::String(i + 1) + ": " + name, i + 1);
    }

    voiceBankSelect.setSelectedId(currentVoiceBankIndex + 1, juce::dontSendNotification);
}

void MainComponent::refreshVoiceLists()
{
    factoryVoices.clear();
    const auto& bank = voiceLibrary.banks[static_cast<std::size_t>(juce::jlimit(0, opaline::kOpalineVoiceBankCount - 1, currentVoiceBankIndex))];
    for (const auto& voice : bank.voices)
        factoryVoices.push_back(voice);

    voiceSelect.clear(juce::dontSendNotification);
    voiceBSelect.clear(juce::dontSendNotification);
    for (int i = 0; i < static_cast<int>(factoryVoices.size()); ++i)
    {
        voiceSelect.addItem(displayNameForVoice(i, factoryVoices[static_cast<std::size_t>(i)]), i + 1);
        voiceBSelect.addItem(displayNameForVoice(i, factoryVoices[static_cast<std::size_t>(i)]), i + 1);
    }

    const int maxVoiceIndex = juce::jmax(0, static_cast<int>(factoryVoices.size()) - 1);
    performanceState.voiceAIndex = juce::jlimit(0, maxVoiceIndex, performanceState.voiceAIndex);
    performanceState.voiceBIndex = juce::jlimit(0, maxVoiceIndex, performanceState.voiceBIndex);
    voiceSelect.setSelectedId(performanceState.voiceAIndex + 1, juce::dontSendNotification);
    voiceBSelect.setSelectedId(performanceState.voiceBIndex + 1, juce::dontSendNotification);
    refreshStatus();
}

void MainComponent::setCurrentVoiceBank(const int bankIndex)
{
    const int safeBank = juce::jlimit(0, opaline::kOpalineVoiceBankCount - 1, bankIndex);
    if (safeBank == currentVoiceBankIndex)
        return;

    currentVoiceBankIndex = safeBank;
    allNotesOff();
    refreshVoiceLists();
    applySelectedVoice();
    populateVoiceBankSelect();
    saveVoiceLibraryState();
}

void MainComponent::storeCurrentPatchToSelectedVoice()
{
    if (factoryVoices.empty())
        return;

    const int voiceIndex = juce::jlimit(0, opaline::kOpalineVoiceBankSize - 1, performanceState.voiceAIndex);
    auto& voice = voiceLibrary.banks[static_cast<std::size_t>(currentVoiceBankIndex)].voices[static_cast<std::size_t>(voiceIndex)];
    voice.patch = opaline::normalizePatch(currentPatch);
    if (voice.name.empty())
        voice.name = "VOICE " + std::to_string(voiceIndex + 1);
    voice.vmem = opaline::encodeCompatibleVmemVoice(voice);
    voice.hasVmem = true;
    factoryVoices[static_cast<std::size_t>(voiceIndex)] = voice;
}

void MainComponent::loadVoiceBankFromFile(const juce::File& file)
{
    if (file.hasFileExtension(".xml"))
    {
        loadVoiceLibraryFromFile(file);
        return;
    }

    const auto bytes = readBinaryFile(file);
    if (bytes.empty())
    {
        statusLabel.setText("Voice load failed: empty file", juce::dontSendNotification);
        return;
    }

    try
    {
        allNotesOff();
        voiceLibrary.banks[static_cast<std::size_t>(currentVoiceBankIndex)] =
            opaline::voiceBankFromSysex(bytes, file.getFileNameWithoutExtension().toStdString());
        populateVoiceBankSelect();
        refreshVoiceLists();
        applySelectedVoice();
        refreshStatus();
        saveVoiceLibraryState();
    }
    catch (const std::exception& e)
    {
        statusLabel.setText("Voice load failed: " + juce::String(e.what()), juce::dontSendNotification);
    }
}

void MainComponent::loadVoiceLibraryFromFile(const juce::File& file)
{
    try
    {
        const auto xml = juce::parseXML(file.loadFileAsString());
        opaline::OpalineVoiceLibrary imported;
        if (xml == nullptr || !voiceLibraryFromXml(*xml, imported))
        {
            statusLabel.setText("Library load failed: invalid file", juce::dontSendNotification);
            return;
        }

        allNotesOff();
        voiceLibrary = std::move(imported);
        currentVoiceBankIndex = juce::jlimit(0, opaline::kOpalineVoiceBankCount - 1, currentVoiceBankIndex);
        populateVoiceBankSelect();
        refreshVoiceLists();
        applySelectedVoice();
        refreshStatus();
        saveVoiceLibraryState();
    }
    catch (const std::exception& e)
    {
        statusLabel.setText("Library load failed: " + juce::String(e.what()), juce::dontSendNotification);
    }
}

void MainComponent::saveCurrentVoiceBankToFile(const juce::File& file)
{
    try
    {
        const auto bytes = opaline::voiceBankToSysex(voiceLibrary.banks[static_cast<std::size_t>(currentVoiceBankIndex)]);
        if (!writeBinaryFile(file, bytes))
        {
            statusLabel.setText("Voice save failed", juce::dontSendNotification);
            return;
        }

        refreshStatus();
        saveVoiceLibraryState();
    }
    catch (const std::exception& e)
    {
        statusLabel.setText("Voice save failed: " + juce::String(e.what()), juce::dontSendNotification);
    }
}

void MainComponent::exportVoiceLibraryToFile(const juce::File& file)
{
    try
    {
        const auto xml = voiceLibraryToXml(voiceLibrary);
        if (xml == nullptr || !xml->writeTo(file))
        {
            statusLabel.setText("Library export failed", juce::dontSendNotification);
            return;
        }

        refreshStatus();
        saveVoiceLibraryState();
    }
    catch (const std::exception& e)
    {
        statusLabel.setText("Library export failed: " + juce::String(e.what()), juce::dontSendNotification);
    }
}

bool MainComponent::restoreSavedVoiceLibraryState()
{
    juce::PropertiesFile settings(settingsOptions());
    const auto xmlText = settings.getValue(kVoiceLibraryXmlSetting);
    if (xmlText.isEmpty())
        return false;

    const auto xml = juce::parseXML(xmlText);
    if (xml == nullptr || !voiceLibraryFromXml(*xml, voiceLibrary))
        return false;

    currentVoiceBankIndex = juce::jlimit(0,
                                         opaline::kOpalineVoiceBankCount - 1,
                                         settings.getIntValue(kVoiceBankIndexSetting, currentVoiceBankIndex));
    performanceState.voiceAIndex = settings.getIntValue(kVoiceAIndexSetting, performanceState.voiceAIndex);
    performanceState.voiceBIndex = settings.getIntValue(kVoiceBIndexSetting, performanceState.voiceBIndex);
    performanceState.mode = static_cast<PerformanceMode>(juce::jlimit(0,
                                                                       2,
                                                                       settings.getIntValue(kPerformanceModeSetting,
                                                                                            static_cast<int>(performanceState.mode))));
    performanceState.dualDetune = juce::jlimit(-16, 16, settings.getIntValue(kDualDetuneSetting, performanceState.dualDetune));
    performanceState.splitPoint = juce::jlimit(0, 127, settings.getIntValue(kSplitPointSetting, performanceState.splitPoint));
    return true;
}

void MainComponent::saveVoiceLibraryState()
{
    juce::PropertiesFile settings(settingsOptions());
    const auto xml = voiceLibraryToXml(voiceLibrary);
    if (xml != nullptr)
        settings.setValue(kVoiceLibraryXmlSetting, xml->toString());

    settings.setValue(kVoiceBankIndexSetting, currentVoiceBankIndex);
    settings.setValue(kVoiceAIndexSetting, performanceState.voiceAIndex);
    settings.setValue(kVoiceBIndexSetting, performanceState.voiceBIndex);
    settings.setValue(kPerformanceModeSetting, static_cast<int>(performanceState.mode));
    settings.setValue(kDualDetuneSetting, performanceState.dualDetune);
    settings.setValue(kSplitPointSetting, performanceState.splitPoint);
    settings.saveIfNeeded();
}

void MainComponent::applySelectedVoice()
{
    const int index = juce::jlimit(0, static_cast<int>(factoryVoices.size()) - 1, voiceSelect.getSelectedId() - 1);
    performanceState.voiceAIndex = index;
    currentPatch = factoryVoices[static_cast<std::size_t>(index)].patch;

    syncUiFromPatch();
    applyPatchToEngine();
}

void MainComponent::syncUiFromPatch()
{
    syncingUi = true;
    algorithmSlider.setValue(currentPatch.algorithm, juce::dontSendNotification);
    feedbackSlider.setValue(currentPatch.feedback, juce::dontSendNotification);
    transposeSlider.setValue(currentPatch.transpose, juce::dontSendNotification);
    lfoWaveSelect.setSelectedId(juce::jlimit(0, 3, currentPatch.lfo.wave) + 1, juce::dontSendNotification);
    lfoSyncButton.setToggleState(currentPatch.lfo.sync, juce::dontSendNotification);
    lfoSpeedSlider.setValue(currentPatch.lfo.speed, juce::dontSendNotification);
    lfoDelaySlider.setValue(currentPatch.lfo.delay, juce::dontSendNotification);
    lfoPitchDepthSlider.setValue(currentPatch.lfo.pitchDepth, juce::dontSendNotification);
    lfoAmpDepthSlider.setValue(currentPatch.lfo.ampDepth, juce::dontSendNotification);
    lfoPitchSensitivitySlider.setValue(currentPatch.lfo.pitchSensitivity, juce::dontSendNotification);
    lfoAmpSensitivitySlider.setValue(currentPatch.lfo.ampSensitivity, juce::dontSendNotification);
    pegRate1Slider.setValue(currentPatch.pitchEnvelope.rate1, juce::dontSendNotification);
    pegRate2Slider.setValue(currentPatch.pitchEnvelope.rate2, juce::dontSendNotification);
    pegRate3Slider.setValue(currentPatch.pitchEnvelope.rate3, juce::dontSendNotification);
    pegLevel1Slider.setValue(currentPatch.pitchEnvelope.level1, juce::dontSendNotification);
    pegLevel2Slider.setValue(currentPatch.pitchEnvelope.level2, juce::dontSendNotification);
    pegLevel3Slider.setValue(currentPatch.pitchEnvelope.level3, juce::dontSendNotification);
    pegGraph.setEnvelope(currentPatch.pitchEnvelope);
    effectReverbSlider.setValue(currentPatch.effects.reverb, juce::dontSendNotification);
    effectMixSlider.setValue(currentPatch.effects.mix, juce::dontSendNotification);
    effectToneSlider.setValue(currentPatch.effects.tone, juce::dontSendNotification);
    effectChorusSlider.setValue(currentPatch.effects.chorus, juce::dontSendNotification);
    effectDelaySlider.setValue(currentPatch.effects.delay, juce::dontSendNotification);
    syncingUi = false;

    refreshLcd();
    refreshAlgorithmAndRoles();
}

void MainComponent::updatePatchFromGlobalControls()
{
    currentPatch.algorithm = static_cast<int>(algorithmSlider.getValue());
    currentPatch.feedback = static_cast<int>(feedbackSlider.getValue());
    currentPatch.transpose = static_cast<int>(transposeSlider.getValue());
    currentPatch.lfo.wave = juce::jlimit(0, 3, lfoWaveSelect.getSelectedId() - 1);
    currentPatch.lfo.sync = lfoSyncButton.getToggleState();
    currentPatch.lfo.speed = static_cast<int>(lfoSpeedSlider.getValue());
    currentPatch.lfo.delay = static_cast<int>(lfoDelaySlider.getValue());
    currentPatch.lfo.pitchDepth = static_cast<int>(lfoPitchDepthSlider.getValue());
    currentPatch.lfo.ampDepth = static_cast<int>(lfoAmpDepthSlider.getValue());
    currentPatch.lfo.pitchSensitivity = static_cast<int>(lfoPitchSensitivitySlider.getValue());
    currentPatch.lfo.ampSensitivity = static_cast<int>(lfoAmpSensitivitySlider.getValue());
    currentPatch.pitchEnvelope.rate1 = static_cast<int>(pegRate1Slider.getValue());
    currentPatch.pitchEnvelope.rate2 = static_cast<int>(pegRate2Slider.getValue());
    currentPatch.pitchEnvelope.rate3 = static_cast<int>(pegRate3Slider.getValue());
    currentPatch.pitchEnvelope.level1 = static_cast<int>(pegLevel1Slider.getValue());
    currentPatch.pitchEnvelope.level2 = static_cast<int>(pegLevel2Slider.getValue());
    currentPatch.pitchEnvelope.level3 = static_cast<int>(pegLevel3Slider.getValue());
    pegGraph.setEnvelope(currentPatch.pitchEnvelope);
    currentPatch.effects.reverb = static_cast<int>(effectReverbSlider.getValue());
    currentPatch.effects.mix = static_cast<int>(effectMixSlider.getValue());
    currentPatch.effects.tone = static_cast<int>(effectToneSlider.getValue());
    currentPatch.effects.chorus = static_cast<int>(effectChorusSlider.getValue());
    currentPatch.effects.delay = static_cast<int>(effectDelaySlider.getValue());
    refreshAlgorithmAndRoles();
}

opaline::OpalinePatch MainComponent::patchForVoiceIndex(const int index) const
{
    if (factoryVoices.empty())
        return opaline::OpalinePatch {};

    const int safeIndex = juce::jlimit(0, static_cast<int>(factoryVoices.size()) - 1, index);
    return factoryVoices[static_cast<std::size_t>(safeIndex)].patch;
}

juce::String MainComponent::performanceVoiceText(const int index) const
{
    if (factoryVoices.empty())
        return "A01 INIT VOICE";

    const int safeIndex = juce::jlimit(0, static_cast<int>(factoryVoices.size()) - 1, index);
    const auto bank = safeIndex < 16 ? "A" : "B";
    const auto number = juce::String(safeIndex % 16 + 1).paddedLeft('0', 2);
    const auto name = juce::String(factoryVoices[static_cast<std::size_t>(safeIndex)].name).substring(0, 12);
    return bank + number + " " + name;
}

void MainComponent::refreshLcd()
{
    switch (performanceState.mode)
    {
        case PerformanceMode::Single:
            lcd.setLines("PLAY SINGLE", performanceVoiceText(performanceState.voiceAIndex));
            break;

        case PerformanceMode::Dual:
            lcd.setLines("DU:" + performanceVoiceText(performanceState.voiceAIndex),
                         juce::String(performanceState.dualDetune).paddedLeft(' ', 2)
                            + ":" + performanceVoiceText(performanceState.voiceBIndex));
            break;

        case PerformanceMode::Split:
            lcd.setLines("SP:" + performanceVoiceText(performanceState.voiceAIndex),
                         juce::String(performanceState.splitPoint).paddedLeft(' ', 2)
                            + ":" + performanceVoiceText(performanceState.voiceBIndex));
            break;
    }
}

void MainComponent::emitProgramNameChanged()
{
    if (onProgramNameChanged)
        onProgramNameChanged(currentProgramName());
}

void MainComponent::refreshPerformanceControls()
{
    performanceModeSelect.setSelectedId(static_cast<int>(performanceState.mode) + 1, juce::dontSendNotification);
    voiceSelect.setSelectedId(performanceState.voiceAIndex + 1, juce::dontSendNotification);
    voiceBSelect.setSelectedId(performanceState.voiceBIndex + 1, juce::dontSendNotification);
    dualDetuneSlider.setValue(performanceState.dualDetune, juce::dontSendNotification);
    splitPointSlider.setValue(performanceState.splitPoint, juce::dontSendNotification);

    const bool dual = performanceState.mode == PerformanceMode::Dual;
    const bool split = performanceState.mode == PerformanceMode::Split;
    voiceBLabel.setVisible(dual || split);
    voiceBSelect.setVisible(dual || split);
    dualDetuneLabel.setVisible(dual);
    dualDetuneSlider.setVisible(dual);
    splitPointLabel.setVisible(split);
    splitPointSlider.setVisible(split);
}

void MainComponent::updatePerformanceFromControls()
{
    performanceState.mode = static_cast<PerformanceMode>(juce::jlimit(1, 3, performanceModeSelect.getSelectedId()) - 1);
    performanceState.voiceAIndex = juce::jmax(0, voiceSelect.getSelectedId() - 1);
    performanceState.voiceBIndex = juce::jmax(0, voiceBSelect.getSelectedId() - 1);
    performanceState.dualDetune = static_cast<int>(dualDetuneSlider.getValue());
    performanceState.splitPoint = static_cast<int>(splitPointSlider.getValue());
}

opalineapp::SynthState MainComponent::captureSynthState() const
{
    opalineapp::SynthState state;
    state.patch = currentPatch;
    state.performance = performanceState;
    state.masterVolume = masterVolume;
    state.renderModel = currentRenderModel();
    return state;
}

void MainComponent::emitSynthStateChanged()
{
    if (!suppressStateCallback && onStateChanged)
        onStateChanged(captureSynthState());
}

void MainComponent::applySynthState(const opalineapp::SynthState& state)
{
    suppressStateCallback = true;
    currentPatch = opaline::normalizePatch(state.patch);
    performanceState = state.performance;
    if (!factoryVoices.empty())
    {
        const int maxVoiceIndex = juce::jmax(0, static_cast<int>(factoryVoices.size()) - 1);
        performanceState.voiceAIndex = juce::jlimit(0, maxVoiceIndex, performanceState.voiceAIndex);

        auto& voice = voiceLibrary.banks[static_cast<std::size_t>(currentVoiceBankIndex)]
                          .voices[static_cast<std::size_t>(performanceState.voiceAIndex)];
        voice.patch = currentPatch;
        voice.vmem = opaline::encodeCompatibleVmemVoice(voice);
        voice.hasVmem = true;
        factoryVoices[static_cast<std::size_t>(performanceState.voiceAIndex)] = voice;
    }
    masterVolume = juce::jlimit(0.0f, 1.0f, state.masterVolume);
    chipRenderModel = state.renderModel == opaline::OpalineRenderModel::ChipHybrid;

    syncingUi = true;
    volumeSlider.setValue(masterVolume, juce::dontSendNotification);
    syncUiFromPatch();
    refreshPerformanceControls();
    refreshEngineModelButton();
    syncingUi = false;

    applyPatchToEngine();
    refreshStatus();
    resized();
    suppressStateCallback = false;
}

void MainComponent::applyPerformanceModeToEngines()
{
    const int voiceCount = performanceState.mode == PerformanceMode::Single ? 8 : 4;
    engine.prepare(audioSampleRate, voiceCount);
    performanceEngineB.prepare(audioSampleRate, 4);
    applyRenderModelToEnginesNoLock();
}

void MainComponent::applyPatchToEngine()
{
    {
        std::lock_guard<std::mutex> lock(engineMutex);
        applyRenderModelToEnginesNoLock();
        engine.setPatch(currentPatch);
        performanceEngineB.setPatch(patchForVoiceIndex(performanceState.voiceBIndex));
        engine.setPitchBend(currentPitchBend);
        engine.setModWheel(currentModWheel);
        performanceEngineB.setPitchBend(juce::jlimit(-1.0, 1.0, currentPitchBend + static_cast<double>(performanceState.dualDetune) / 64.0));
        performanceEngineB.setModWheel(currentModWheel);
    }
    refreshLcd();
    emitProgramNameChanged();
    emitSynthStateChanged();
    if (onRenderModelChanged)
        onRenderModelChanged(currentRenderModel());
}

opaline::OpalineRenderModel MainComponent::currentRenderModel() const
{
    return chipRenderModel ? opaline::OpalineRenderModel::ChipHybrid : opaline::OpalineRenderModel::Current;
}

void MainComponent::applyRenderModelToEnginesNoLock()
{
    const auto model = currentRenderModel();
    engine.setRenderModel(model);
    performanceEngineB.setRenderModel(model);
}

void MainComponent::refreshEngineModelButton()
{
    engineModelButton.setButtonText(chipRenderModel ? "NEW" : "OLD");
    engineModelButton.setToggleState(chipRenderModel, juce::dontSendNotification);
    engineModelButton.setColour(juce::TextButton::buttonColourId,
                                chipRenderModel ? juce::Colour(0xff243d38) : juce::Colour(0xff1c1a15));
    engineModelButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff0aa878));
    engineModelButton.setColour(juce::TextButton::textColourOffId, chipRenderModel ? juce::Colours::white : kTextPrimary);
    engineModelButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}

void MainComponent::setupSlider(juce::Slider& slider,
                                const double min,
                                const double max,
                                const double step,
                                const double value,
                                const juce::Slider::SliderStyle style)
{
    slider.setRange(min, max, step);
    slider.setValue(value, juce::dontSendNotification);
    slider.setSliderStyle(style);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 16);
    slider.setMouseDragSensitivity(130);
    if (style == juce::Slider::RotaryHorizontalVerticalDrag)
        slider.setLookAndFeel(&opalineLookAndFeel);
}

void MainComponent::setupLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setFont(juce::FontOptions(13.0f, juce::Font::plain));
    label.setColour(juce::Label::textColourId, kTextPrimary);
}

void MainComponent::setupComboBox(juce::ComboBox& comboBox)
{
    comboBox.setLookAndFeel(&opalineLookAndFeel);
    comboBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1c1a15));
    comboBox.setColour(juce::ComboBox::textColourId, kTextPrimary);
    comboBox.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff3a3529));
    comboBox.setColour(juce::ComboBox::arrowColourId, kTextMuted);
}

void MainComponent::populateAudioOutputSelect()
{
    audioOutputChoices.clear();
    audioOutputSelect.clear(juce::dontSendNotification);
    audioOutputSelect.addItem("Audio: Default", 1);

    juce::OwnedArray<juce::AudioIODeviceType> types;
    juce::AudioDeviceManager tempManager;
    tempManager.createAudioDeviceTypes(types);

    int id = 2;
    for (auto* type : types)
    {
        if (type == nullptr)
            continue;

        type->scanForDevices();
        const auto names = type->getDeviceNames(false);
        for (const auto& name : names)
        {
            audioOutputChoices.push_back({ type->getTypeName(), name });
            audioOutputSelect.addItem("Audio: " + type->getTypeName() + " - " + name, id++);
        }
    }

    audioOutputSelect.setSelectedId(1, juce::dontSendNotification);
    restoreAudioOutputSelection();
}

void MainComponent::populateMidiInputSelect()
{
    midiInputDevices = juce::MidiInput::getAvailableDevices();
    midiInputSelect.clear(juce::dontSendNotification);
    midiInputSelect.addItem("MIDI: Off", 1);
    midiInputSelect.addItem("MIDI: All Inputs", 2);

    int id = 3;
    for (const auto& device : midiInputDevices)
        midiInputSelect.addItem("MIDI: " + device.name, id++);

    if (midiInputDevices.isEmpty())
        midiInputSelect.setText("MIDI: No Input", juce::dontSendNotification);
    else
        midiInputSelect.setSelectedId(1, juce::dontSendNotification);

    restoreMidiInputSelection();
}

void MainComponent::refreshAlgorithmAndRoles()
{
    algorithmView.setAlgorithm(currentPatch.algorithm, currentPatch.feedback);
    for (int i = 0; i < opaline::kOperatorCount; ++i)
    {
        auto& panel = operatorPanels[static_cast<std::size_t>(i)];
        if (panel)
        {
            panel->setOperator(currentPatch.operators[static_cast<std::size_t>(i)]);
            panel->setRole(operatorRole(currentPatch, i));
        }
    }
}

void MainComponent::refreshStatus()
{
    const auto modeName = performanceState.mode == PerformanceMode::Single ? "SINGLE"
                        : performanceState.mode == PerformanceMode::Dual ? "DUAL"
                        : "SPLIT";
    statusLabel.setText(audioStatus + "   " + midiStatus + "   Perf: " + modeName
                            + "   Engine: " + (chipRenderModel ? "NEW" : "OLD")
                            + "   Bank: " + juce::String(currentVoiceBankIndex + 1)
                            + "   Voices: " + juce::String(factoryVoices.size())
                            + "   LFO: " + lfoWaveName(currentPatch.lfo.wave),
                        juce::dontSendNotification);
}

bool MainComponent::ensureAudioStarted()
{
    if (hostMode == HostMode::PluginEditor)
        return true;

    if (audioStarted)
        return true;

    audioDeviceManager = std::make_unique<juce::AudioDeviceManager>();
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    const int outputId = audioOutputSelect.getSelectedId();
    juce::String requestedTypeName;
    if (outputId >= 2)
    {
        const auto index = static_cast<std::size_t>(outputId - 2);
        if (index < audioOutputChoices.size())
        {
            const auto& choice = audioOutputChoices[index];
            requestedTypeName = choice.typeName;
            audioDeviceManager->setCurrentAudioDeviceType(choice.typeName, true);
            setup.outputDeviceName = choice.deviceName;
        }
    }
   #if JUCE_WINDOWS
    else
    {
        audioDeviceManager->setCurrentAudioDeviceType("Windows Audio (Low Latency Mode)", true);
    }
   #endif

    setup.inputDeviceName = {};
    juce::String lowLatencyFailure;
    auto tryOpenAudio = [this, &setup](const juce::String& typeName, const int bufferSize)
    {
        if (typeName.isNotEmpty())
            audioDeviceManager->setCurrentAudioDeviceType(typeName, true);

        audioDeviceManager->closeAudioDevice();
        setup.bufferSize = bufferSize;
        return audioDeviceManager->initialise(0, 2, nullptr, false, {}, &setup);
    };

    auto currentBufferSize = [this]() -> int
    {
        if (auto* device = audioDeviceManager->getCurrentAudioDevice())
            return device->getCurrentBufferSizeSamples();

        return 0;
    };

    juce::String error;
    const auto requestedOpenTypeName = requestedTypeName.isNotEmpty()
        ? requestedTypeName
       #if JUCE_WINDOWS
        : juce::String("Windows Audio (Low Latency Mode)");
       #else
        : juce::String();
       #endif

    error = tryOpenAudio(requestedOpenTypeName, kPreferredAudioBufferSize);
    if (error.isEmpty())
    {
        const int actualBufferSize = currentBufferSize();
        if (actualBufferSize > kMaxLowLatencyAudioBufferSize)
        {
            const auto displayTypeName = requestedOpenTypeName.isNotEmpty()
                ? requestedOpenTypeName
                : juce::String("Audio");
            lowLatencyFailure = displayTypeName + " "
                + juce::String(kPreferredAudioBufferSize) + "->"
                + juce::String(actualBufferSize) + "smpl";
        }
    }

    if (error.isNotEmpty() && requestedTypeName.isNotEmpty())
        error = tryOpenAudio(requestedTypeName, 0);

   #if JUCE_WINDOWS
    if (error.isNotEmpty() && outputId < 2 && lowLatencyFailure.isEmpty())
        error = tryOpenAudio("Windows Audio (Low Latency Mode)", 0);

    if (error.isNotEmpty() && outputId < 2 && lowLatencyFailure.isEmpty())
        error = tryOpenAudio("Windows Audio", kPreferredAudioBufferSize);

    if (error.isNotEmpty() && outputId < 2 && lowLatencyFailure.isEmpty())
        error = tryOpenAudio("Windows Audio", 0);

    if (error.isNotEmpty() && outputId < 2 && lowLatencyFailure.isEmpty())
        error = tryOpenAudio("DirectSound", 0);
   #endif
    if (error.isNotEmpty() && requestedTypeName.isEmpty() && lowLatencyFailure.isEmpty())
        error = tryOpenAudio({}, 0);

    if (error.isNotEmpty())
    {
        audioStatus = "Audio: " + error;
        refreshStatus();
        audioDeviceManager = nullptr;
        return false;
    }

    audioDeviceManager->addAudioCallback(&audioSourcePlayer);
    audioSourcePlayer.setSource(this);
    audioStarted = true;
    if (auto* device = audioDeviceManager->getCurrentAudioDevice())
    {
        const int actualBufferSize = device->getCurrentBufferSizeSamples();
        audioStatus = "Audio: " + device->getTypeName()
            + " " + juce::String(actualBufferSize) + "smpl";

        if (actualBufferSize > kMaxLowLatencyAudioBufferSize && lowLatencyFailure.isNotEmpty())
            audioStatus += " >128";
    }
    else
    {
        audioStatus = "Audio: on";
    }
    return true;
}

bool MainComponent::startPlayback()
{
    if (powerOn)
        return true;

    if (hostMode == HostMode::PluginEditor)
    {
        powerOn = true;
        return true;
    }

    if (!ensureAudioStarted())
        return false;

    powerOn = true;
    powerButton.setButtonText("ON");
    powerButton.setToggleState(true, juce::dontSendNotification);
    powerButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0aa878));
    return true;
}

void MainComponent::restartAudioOutput()
{
    if (!audioStarted)
    {
        ensureAudioStarted();
        return;
    }

    const bool shouldResume = powerOn;
    allNotesOff();
    powerOn = false;
    audioSourcePlayer.setSource(nullptr);

    if (audioDeviceManager)
    {
        audioDeviceManager->removeAudioCallback(&audioSourcePlayer);
        audioDeviceManager->closeAudioDevice();
    }

    audioStarted = false;
    audioDeviceManager = nullptr;
    audioStatus = "Audio: off";

    if (shouldResume)
        startPlayback();
    else
        ensureAudioStarted();
}

void MainComponent::connectMidiInputs()
{
    for (auto& input : midiInputs)
    {
        if (input)
            input->stop();
    }
    midiInputs.clear();

    midiInputDevices = juce::MidiInput::getAvailableDevices();
    const auto selectedId = midiInputSelect.getSelectedId();

    if (selectedId <= 1)
    {
        midiStatus = "MIDI: off";
        return;
    }

    const auto connectDevice = [this](const juce::MidiDeviceInfo& device)
    {
        if (auto input = juce::MidiInput::openDevice(device.identifier, this))
        {
            input->start();
            midiInputs.push_back(std::move(input));
        }
    };

    if (selectedId >= 3)
    {
        const int index = selectedId - 3;
        if (juce::isPositiveAndBelow(index, midiInputDevices.size()))
            connectDevice(midiInputDevices[index]);
    }
    else
    {
        for (const auto& device : midiInputDevices)
            connectDevice(device);
    }

    if (midiInputs.empty())
    {
        midiStatus = midiInputDevices.isEmpty() ? "MIDI: no input" : "MIDI: open failed";
        return;
    }

    if (selectedId >= 3 && midiInputs.size() == 1)
        midiStatus = "MIDI: " + midiInputDevices[selectedId - 3].name;
    else
    {
        midiStatus = "MIDI: " + juce::String(static_cast<int>(midiInputs.size())) + " input";
        if (midiInputs.size() != 1)
            midiStatus += "s";
    }
}

void MainComponent::restoreMidiInputSelection()
{
    juce::PropertiesFile settings(settingsOptions());
    const auto savedIdentifier = settings.getValue(kMidiInputIdentifierSetting);
    if (savedIdentifier.isEmpty())
        return;

    for (int i = 0; i < midiInputDevices.size(); ++i)
    {
        if (midiInputDevices[i].identifier == savedIdentifier)
        {
            midiInputSelect.setSelectedId(i + 3, juce::dontSendNotification);
            connectMidiInputs();
            refreshStatus();
            return;
        }
    }
}

void MainComponent::restoreAudioOutputSelection()
{
    juce::PropertiesFile settings(settingsOptions());
    const auto savedType = settings.getValue(kAudioOutputTypeSetting);
    const auto savedDevice = settings.getValue(kAudioOutputDeviceSetting);
    if (savedType.isEmpty() || savedDevice.isEmpty())
        return;

    for (std::size_t i = 0; i < audioOutputChoices.size(); ++i)
    {
        const auto& choice = audioOutputChoices[i];
        if (choice.typeName == savedType && choice.deviceName == savedDevice)
        {
            audioOutputSelect.setSelectedId(static_cast<int>(i) + 2, juce::dontSendNotification);
            return;
        }
    }
}

void MainComponent::saveAudioOutputSelection() const
{
    juce::PropertiesFile settings(settingsOptions());
    const int selectedId = audioOutputSelect.getSelectedId();

    if (selectedId >= 2)
    {
        const auto index = static_cast<std::size_t>(selectedId - 2);
        if (index < audioOutputChoices.size())
        {
            const auto& choice = audioOutputChoices[index];
            settings.setValue(kAudioOutputTypeSetting, choice.typeName);
            settings.setValue(kAudioOutputDeviceSetting, choice.deviceName);
            settings.saveIfNeeded();
            return;
        }
    }

    settings.removeValue(kAudioOutputTypeSetting);
    settings.removeValue(kAudioOutputDeviceSetting);
    settings.saveIfNeeded();
}

void MainComponent::saveMidiInputSelection() const
{
    juce::PropertiesFile settings(settingsOptions());
    const int selectedId = midiInputSelect.getSelectedId();

    if (selectedId >= 3)
    {
        const int index = selectedId - 3;
        if (juce::isPositiveAndBelow(index, midiInputDevices.size()))
        {
            settings.setValue(kMidiInputIdentifierSetting, midiInputDevices[index].identifier);
            settings.saveIfNeeded();
            return;
        }
    }

    settings.removeValue(kMidiInputIdentifierSetting);
    settings.saveIfNeeded();
}

void MainComponent::noteOn(const int note, const int velocity)
{
    if (!powerOn && !startPlayback())
        return;

    if (juce::isPositiveAndBelow(note, static_cast<int>(gMidiUiHeldNotes.size())))
    {
        gMidiUiHeldNotes[static_cast<std::size_t>(note)].store(true, std::memory_order_relaxed);
        gMidiUiHeldVelocities[static_cast<std::size_t>(note)].store(juce::jlimit(0, 127, velocity), std::memory_order_relaxed);
        repaintKeyboardAsync();
    }

    if (hostMode == HostMode::PluginEditor)
    {
        if (onNoteOn)
            onNoteOn(note, velocity);
        return;
    }

    std::lock_guard<std::mutex> lock(engineMutex);
    performNoteOnNoLock(note, velocity);
}

void MainComponent::noteOff(const int note)
{
    if (juce::isPositiveAndBelow(note, static_cast<int>(gMidiUiHeldNotes.size())))
    {
        gMidiUiHeldNotes[static_cast<std::size_t>(note)].store(false, std::memory_order_relaxed);
        gMidiUiHeldVelocities[static_cast<std::size_t>(note)].store(0, std::memory_order_relaxed);
        repaintKeyboardAsync();
    }

    if (hostMode == HostMode::PluginEditor)
    {
        if (onNoteOff)
            onNoteOff(note);
        return;
    }

    std::lock_guard<std::mutex> lock(engineMutex);
    performNoteOffNoLock(note);
}

void MainComponent::performNoteOnNoLock(const int note, const int velocity)
{
    switch (performanceState.mode)
    {
        case PerformanceMode::Single:
            engine.noteOn(note, velocity);
            break;

        case PerformanceMode::Dual:
            engine.noteOn(note, velocity);
            performanceEngineB.noteOn(note, velocity);
            break;

        case PerformanceMode::Split:
            if (note <= performanceState.splitPoint)
                engine.noteOn(note, velocity);
            else
                performanceEngineB.noteOn(note, velocity);
            break;
    }
}

void MainComponent::performNoteOffNoLock(const int note)
{
    engine.noteOff(note);
    performanceEngineB.noteOff(note);
}

void MainComponent::allNotesOff()
{
    for (auto& held : gMidiUiHeldNotes)
        held.store(false, std::memory_order_relaxed);
    for (auto& velocity : gMidiUiHeldVelocities)
        velocity.store(0, std::memory_order_relaxed);
    pcKeyboardHeldNotes.fill(false);
    pcKeyboardHeldVelocities.fill(0);
    repaintKeyboardAsync();

    if (hostMode == HostMode::PluginEditor)
    {
        if (onAllNotesOff)
            onAllNotesOff();
        return;
    }

    std::lock_guard<std::mutex> lock(engineMutex);
    engine.panic();
    performanceEngineB.panic();
}

bool MainComponent::isMidiUiNoteHeld(const int note) const
{
    if (!juce::isPositiveAndBelow(note, static_cast<int>(gMidiUiHeldNotes.size())))
        return false;

    return gMidiUiHeldNotes[static_cast<std::size_t>(note)].load(std::memory_order_relaxed)
        || pcKeyboardHeldNotes[static_cast<std::size_t>(note)];
}

int MainComponent::heldVelocityForNote(const int note) const
{
    if (!juce::isPositiveAndBelow(note, static_cast<int>(gMidiUiHeldVelocities.size())))
        return 0;

    const auto index = static_cast<std::size_t>(note);
    const int midiVelocity = gMidiUiHeldVelocities[index].load(std::memory_order_relaxed);
    const int pcVelocity = pcKeyboardHeldVelocities[index];
    return juce::jmax(midiVelocity, pcVelocity);
}

void MainComponent::repaintKeyboardAsync()
{
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        keyboard.repaint();
        return;
    }

    juce::MessageManager::callAsync([this]
    {
        keyboard.repaint();
    });
}

void MainComponent::syncPcKeyboardNotes()
{
    std::array<bool, 128> shouldHold {};
    if (!hasKeyboardFocus(true))
    {
        bool changed = false;
        for (int note = 0; note < static_cast<int>(pcKeyboardHeldNotes.size()); ++note)
        {
            const auto index = static_cast<std::size_t>(note);
            if (!pcKeyboardHeldNotes[index])
                continue;

            pcKeyboardHeldNotes[index] = false;
            pcKeyboardHeldVelocities[index] = 0;
            changed = true;
            noteOff(note);
        }

        if (changed)
            keyboard.repaint();
        return;
    }

    for (const auto& mapping : kPcKeyboardMap)
    {
        const int note = mapping.note + kPcKeyboardTranspose;
        if (juce::isPositiveAndBelow(note, static_cast<int>(shouldHold.size()))
            && isPcKeyCurrentlyDown(mapping.keyCode))
        {
            shouldHold[static_cast<std::size_t>(note)] = true;
        }
    }

    bool changed = false;
    for (int note = 0; note < static_cast<int>(pcKeyboardHeldNotes.size()); ++note)
    {
        const auto index = static_cast<std::size_t>(note);
        if (shouldHold[index] == pcKeyboardHeldNotes[index])
            continue;

        pcKeyboardHeldNotes[index] = shouldHold[index];
        pcKeyboardHeldVelocities[index] = shouldHold[index] ? 108 : 0;
        changed = true;
        if (shouldHold[index])
            noteOn(note, 108);
        else
            noteOff(note);
    }

    if (changed)
        keyboard.repaint();
}

void MainComponent::setExternalControllerState(const double pitchBend, const double modWheel)
{
    const double safePitchBend = juce::jlimit(-1.0, 1.0, pitchBend);
    const double safeModWheel = juce::jlimit(0.0, 1.0, modWheel);

    currentPitchBend = safePitchBend;
    currentModWheel = safeModWheel;
    pitchWheelSlider.setValue(safePitchBend, juce::dontSendNotification);
    modWheelSlider.setValue(safeModWheel, juce::dontSendNotification);
}

void MainComponent::timerCallback()
{
    syncPcKeyboardNotes();
}

void MainComponent::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        const int note = message.getNoteNumber();
        const int velocity = juce::jlimit(1, 127, static_cast<int>(message.getVelocity()));
        if (juce::isPositiveAndBelow(note, static_cast<int>(gMidiUiHeldNotes.size())))
        {
            gMidiUiHeldNotes[static_cast<std::size_t>(note)].store(true, std::memory_order_relaxed);
            gMidiUiHeldVelocities[static_cast<std::size_t>(note)].store(velocity, std::memory_order_relaxed);
            repaintKeyboardAsync();
        }

        if (!powerOn)
        {
            juce::MessageManager::callAsync([this, note, velocity]
            {
                noteOn(note, velocity);
            });
            return;
        }

        noteOn(note, velocity);
    }
    else if (message.isNoteOff())
    {
        const int note = message.getNoteNumber();
        if (juce::isPositiveAndBelow(note, static_cast<int>(gMidiUiHeldNotes.size())))
        {
            gMidiUiHeldNotes[static_cast<std::size_t>(note)].store(false, std::memory_order_relaxed);
            gMidiUiHeldVelocities[static_cast<std::size_t>(note)].store(0, std::memory_order_relaxed);
            repaintKeyboardAsync();
        }

        noteOff(note);
    }
    else if (message.isPitchWheel())
    {
        const double value = juce::jlimit(-1.0,
                                          1.0,
                                          (static_cast<double>(message.getPitchWheelValue()) - 8192.0) / 8192.0);
        currentPitchBend = value;
        std::lock_guard<std::mutex> lock(engineMutex);
        engine.setPitchBend(value);
        performanceEngineB.setPitchBend(juce::jlimit(-1.0, 1.0, value + static_cast<double>(performanceState.dualDetune) / 64.0));
        juce::MessageManager::callAsync([this, value]
        {
            pitchWheelSlider.setValue(value, juce::dontSendNotification);
        });
    }
    else if (message.isController() && message.getControllerNumber() == 1)
    {
        const double value = static_cast<double>(message.getControllerValue()) / 127.0;
        currentModWheel = value;
        std::lock_guard<std::mutex> lock(engineMutex);
        engine.setModWheel(value);
        performanceEngineB.setModWheel(value);
        juce::MessageManager::callAsync([this, value]
        {
            modWheelSlider.setValue(value, juce::dontSendNotification);
        });
    }
    else if (message.isAllNotesOff())
    {
        allNotesOff();
    }
}

void MainComponent::comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == &voiceSelect)
    {
        allNotesOff();
        applySelectedVoice();
        saveVoiceLibraryState();
    }
    else if (comboBoxThatHasChanged == &voiceBankSelect)
    {
        setCurrentVoiceBank(voiceBankSelect.getSelectedId() - 1);
    }
    else if (comboBoxThatHasChanged == &performanceModeSelect || comboBoxThatHasChanged == &voiceBSelect)
    {
        allNotesOff();
        updatePerformanceFromControls();
        refreshPerformanceControls();
        {
            std::lock_guard<std::mutex> lock(engineMutex);
            applyPerformanceModeToEngines();
        }
        applyPatchToEngine();
        refreshStatus();
        resized();
    }
    else if (comboBoxThatHasChanged == &lfoWaveSelect && !syncingUi)
    {
        updatePatchFromGlobalControls();
        applyPatchToEngine();
        refreshStatus();
    }
    else if (comboBoxThatHasChanged == &audioOutputSelect)
    {
        saveAudioOutputSelection();
        restartAudioOutput();
        refreshStatus();
    }
    else if (comboBoxThatHasChanged == &midiInputSelect)
    {
        connectMidiInputs();
        saveMidiInputSelection();
        refreshStatus();
    }
}

void MainComponent::buttonClicked(juce::Button* button)
{
    if (button == &powerButton)
    {
        if (!powerOn)
        {
            startPlayback();
            return;
        }

        powerOn = false;
        powerButton.setButtonText("OFF");
        powerButton.setToggleState(false, juce::dontSendNotification);
        powerButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff172b27));
        allNotesOff();
    }
    else if (button == &lfoSyncButton && !syncingUi)
    {
        updatePatchFromGlobalControls();
        applyPatchToEngine();
    }
    else if (button == &engineModelButton)
    {
        chipRenderModel = engineModelButton.getToggleState();
        refreshEngineModelButton();
        allNotesOff();
        applyPatchToEngine();
        refreshStatus();
    }
    else if (button == &loadVoiceBankButton)
    {
        fileChooser = std::make_unique<juce::FileChooser>("Load compatible voice bank or library",
                                                          juce::File {},
                                                          "*.syx;*.xml");
        fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [this](const juce::FileChooser& chooser)
                                 {
                                     const auto file = chooser.getResult();
                                     if (file.existsAsFile())
                                         loadVoiceBankFromFile(file);
                                 });
    }
    else if (button == &saveVoiceBankButton)
    {
        const auto bankName = juce::String(voiceLibrary.banks[static_cast<std::size_t>(currentVoiceBankIndex)].name)
                                  .retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_ ");
        const auto defaultName = (bankName.isEmpty() ? juce::String("OpalineFM_Bank_") + juce::String(currentVoiceBankIndex + 1)
                                                     : bankName)
            + ".syx";
        fileChooser = std::make_unique<juce::FileChooser>("Save current compatible voice bank",
                                                          juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(defaultName),
                                                          "*.syx");
        fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                                 [this](const juce::FileChooser& chooser)
                                 {
                                     auto file = chooser.getResult();
                                     if (file == juce::File {})
                                         return;
                                     if (!file.hasFileExtension(".syx"))
                                         file = file.withFileExtension(".syx");
                                     saveCurrentVoiceBankToFile(file);
                                 });
    }
    else if (button == &exportVoiceLibraryButton)
    {
        fileChooser = std::make_unique<juce::FileChooser>("Export all Opaline voice data",
                                                          juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                              .getChildFile("OpalineFM_Voice_Library.opalinelibrary.xml"),
                                                          "*.xml");
        fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                                 [this](const juce::FileChooser& chooser)
                                 {
                                     auto file = chooser.getResult();
                                     if (file == juce::File {})
                                         return;
                                     if (!file.hasFileExtension(".xml"))
                                         file = file.withFileExtension(".xml");
                                     exportVoiceLibraryToFile(file);
                                 });
    }
    else if (button == &storeVoiceButton)
    {
        storeCurrentPatchToSelectedVoice();
        refreshVoiceLists();
        voiceSelect.setSelectedId(performanceState.voiceAIndex + 1, juce::dontSendNotification);
        voiceBSelect.setSelectedId(performanceState.voiceBIndex + 1, juce::dontSendNotification);
        statusLabel.setText("Voice stored", juce::dontSendNotification);
        refreshStatus();
        saveVoiceLibraryState();
    }
}

void MainComponent::sliderValueChanged(juce::Slider* slider)
{
    if (slider == &volumeSlider)
    {
        masterVolume = static_cast<float>(volumeSlider.getValue());
        emitSynthStateChanged();
    }
    else if (slider == &transposeSlider)
    {
        if (!syncingUi)
        {
            updatePatchFromGlobalControls();
            applyPatchToEngine();
        }
    }
    else if (slider == &pitchWheelSlider)
    {
        currentPitchBend = pitchWheelSlider.getValue();
        if (hostMode == HostMode::PluginEditor)
        {
            if (onPitchBend)
                onPitchBend(currentPitchBend);
            return;
        }

        std::lock_guard<std::mutex> lock(engineMutex);
        engine.setPitchBend(currentPitchBend);
        performanceEngineB.setPitchBend(juce::jlimit(-1.0, 1.0, currentPitchBend + static_cast<double>(performanceState.dualDetune) / 64.0));
    }
    else if (slider == &modWheelSlider)
    {
        currentModWheel = modWheelSlider.getValue();
        if (hostMode == HostMode::PluginEditor)
        {
            if (onModWheel)
                onModWheel(currentModWheel);
            return;
        }

        std::lock_guard<std::mutex> lock(engineMutex);
        engine.setModWheel(modWheelSlider.getValue());
        performanceEngineB.setModWheel(modWheelSlider.getValue());
    }
    else if (slider == &dualDetuneSlider || slider == &splitPointSlider)
    {
        if (!syncingUi)
        {
            updatePerformanceFromControls();
            applyPatchToEngine();
            refreshStatus();
        }
    }
    else if (!syncingUi)
    {
        updatePatchFromGlobalControls();
        applyPatchToEngine();
        refreshStatus();
    }
}

void MainComponent::sliderDragEnded(juce::Slider* slider)
{
    if (slider == &pitchWheelSlider)
    {
        pitchWheelSlider.setValue(0.0, juce::sendNotificationSync);
    }
}
