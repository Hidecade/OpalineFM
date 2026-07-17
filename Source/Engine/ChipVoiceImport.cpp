#include "Engine/ChipVoiceImport.h"

#include "Engine/OpalineTables.h"
#include "Engine/OpalineTypes.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace opaline
{
namespace
{
struct ChipOperator
{
    int multiplier = 1;
    int detune = 0;
    int detune2 = 0;
    int totalLevel = 127;
    int rateScale = 0;
    int attackRate = 0;
    int decay1Rate = 0;
    int decay2Rate = 0;
    int releaseRate = 0;
    int sustainLevel = 15;
    int ssgEg = 0;
    bool ampModEnable = false;
    bool enabled = true;
};

struct ChipVoice
{
    std::string name;
    int algorithm = 0;
    int feedback = 0;
    int lfoSpeed = 0;
    int ampDepth = 0;
    int pitchDepth = 0;
    int lfoWave = 0;
    int ampSensitivity = 0;
    int pitchSensitivity = 0;
    std::array<ChipOperator, kOperatorCount> operators {};
};

std::string lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char ch)
    {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

std::string normalizedExtension(std::string extension)
{
    extension = lowercase(std::move(extension));
    if (!extension.empty() && extension.front() == '.')
        extension.erase(extension.begin());
    return extension;
}

std::string trim(const std::string& text)
{
    const auto first = std::find_if_not(text.begin(), text.end(), [](const unsigned char ch) { return std::isspace(ch); });
    if (first == text.end())
        return {};
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](const unsigned char ch) { return std::isspace(ch); }).base();
    return std::string(first, last);
}

std::string limitedName(const std::string& name, const std::string& fallback)
{
    auto result = trim(name.empty() ? fallback : name);
    if (result.empty())
        result = "CHIP VOICE";
    if (result.size() > 10)
        result.resize(10);
    return result;
}

void requireRange(const int value, const int low, const int high, const char* field)
{
    if (value < low || value > high)
        throw std::runtime_error(std::string(field) + " is out of range.");
}

int decodeRegisterDetune(const int value)
{
    requireRange(value, 0, 7, "DT1");
    const int amount = value & 3;
    return (value & 4) != 0 ? -amount : amount;
}

int decodeCenteredDetune(const int value)
{
    requireRange(value, 0, 7, "DT1");
    return value == 7 ? 0 : value - 3;
}

int nearestRatioIndex(const int multiplier, const int detune2, bool& approximated)
{
    requireRange(multiplier, 0, 15, "MUL");
    requireRange(detune2, 0, 3, "DT2");
    static constexpr std::array<double, 4> dt2Factors { 1.0, 1.41421356237, 1.57, 1.73 };
    const double baseRatio = multiplier == 0 ? 0.5 : static_cast<double>(multiplier);
    const double target = baseRatio * dt2Factors[static_cast<std::size_t>(detune2)];
    const auto& ratios = opalineRatios();

    int bestIndex = 0;
    double bestDistance = std::numeric_limits<double>::max();
    for (std::size_t index = 0; index < ratios.size(); ++index)
    {
        const double distance = std::abs(std::log2(ratios[index] / target));
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestIndex = static_cast<int>(index);
        }
    }
    approximated = bestDistance > 0.0001;
    return bestIndex;
}

OpalinePatchWithMetadata convertVoice(const ChipVoice& source,
                                      const std::string& fallbackName,
                                      std::vector<std::string>& warnings)
{
    OpalinePatchWithMetadata result;
    result.name = limitedName(source.name, fallbackName);
    result.effectsEnabled = false;
    result.hasVmem = false;

    OpalinePatch patch;
    patch.algorithm = source.algorithm + 1;
    patch.feedback = source.feedback;
    patch.transpose = 0;
    patch.pitchEnvelope = OpalinePitchEnvelopeParams {};
    patch.effects = OpalineEffects {};
    patch.lfo.speed = source.lfoSpeed;
    patch.lfo.delay = 0;
    patch.lfo.pitchDepth = source.pitchDepth;
    patch.lfo.ampDepth = source.ampDepth;
    patch.lfo.pitchSensitivity = source.pitchSensitivity;
    patch.lfo.ampSensitivity = source.ampSensitivity;
    patch.lfo.wave = source.lfoWave;
    patch.lfo.sync = false;

    bool warnedSsgEg = false;
    bool warnedRatio = false;
    for (std::size_t index = 0; index < source.operators.size(); ++index)
    {
        const auto& input = source.operators[index];
        auto& output = patch.operators[index];
        bool approximated = false;
        output.ratioIndex = nearestRatioIndex(input.multiplier, input.detune2, approximated);
        output.detune = input.detune;
        output.level = 99 - std::min(input.totalLevel, 99);
        output.rateScale = input.rateScale;
        output.levelScale = 0;
        output.velocity = 0;
        output.ampModEnable = input.ampModEnable;
        output.enabled = input.enabled;
        output.envelope.attackRate = input.attackRate;
        output.envelope.decay1Rate = input.decay1Rate;
        output.envelope.decay2Rate = input.decay2Rate;
        output.envelope.releaseRate = input.releaseRate;
        output.envelope.decay1Level = 15 - std::min(input.sustainLevel, 15);
        warnedRatio = warnedRatio || approximated;
        warnedSsgEg = warnedSsgEg || input.ssgEg != 0;
    }

    if (warnedRatio)
        warnings.push_back("DT2 or multiplier was mapped to the nearest Opaline ratio.");
    if (warnedSsgEg)
        warnings.push_back("SSG-EG is not supported and was ignored.");

    result.patch = normalizePatch(patch);
    return result;
}

ChipOperator readTfiOperator(const std::uint8_t* data)
{
    ChipOperator op;
    op.multiplier = data[0];
    requireRange(op.multiplier, 0, 15, "MUL");
    requireRange(data[1], 0, 6, "DT");
    op.detune = static_cast<int>(data[1]) - 3;
    op.totalLevel = data[2];
    op.rateScale = data[3];
    op.attackRate = data[4];
    op.decay1Rate = data[5];
    op.decay2Rate = data[6];
    op.releaseRate = data[7];
    op.sustainLevel = data[8];
    op.ssgEg = data[9];
    requireRange(op.totalLevel, 0, 127, "TL");
    requireRange(op.rateScale, 0, 3, "RS");
    requireRange(op.attackRate, 0, 31, "AR");
    requireRange(op.decay1Rate, 0, 31, "DR");
    requireRange(op.decay2Rate, 0, 31, "SR");
    requireRange(op.releaseRate, 0, 15, "RR");
    requireRange(op.sustainLevel, 0, 15, "SL");
    requireRange(op.ssgEg, 0, 15, "SSG-EG");
    return op;
}

ChipVoice parseTfiOrVgi(const std::vector<std::uint8_t>& bytes, const bool isVgi)
{
    const std::size_t expectedSize = isVgi ? 43 : 42;
    if (bytes.size() != expectedSize)
        throw std::runtime_error(isVgi ? "VGI file must be 43 bytes." : "TFI file must be 42 bytes.");

    ChipVoice voice;
    voice.algorithm = bytes[0];
    voice.feedback = bytes[1];
    requireRange(voice.algorithm, 0, 7, "Algorithm");
    requireRange(voice.feedback, 0, 7, "Feedback");

    const std::size_t operatorOffset = isVgi ? 3 : 2;
    if (isVgi)
    {
        voice.pitchSensitivity = bytes[2] & 0x07;
        voice.ampSensitivity = (bytes[2] >> 4) & 0x03;
    }

    // TFI/VGI stores Yamaha slots as S1, S3, S2, S4. Opaline numbers carriers outward from OP1.
    static constexpr std::array<int, 4> targetOrder { 3, 1, 2, 0 };
    for (std::size_t block = 0; block < targetOrder.size(); ++block)
    {
        voice.operators[static_cast<std::size_t>(targetOrder[block])] =
            readTfiOperator(bytes.data() + operatorOffset + block * 10);
    }
    return voice;
}

std::vector<int> parseNumbers(const std::string& text)
{
    std::istringstream stream(text);
    std::vector<int> values;
    int value = 0;
    while (stream >> value)
        values.push_back(value);
    return values;
}

void requireCount(const std::vector<int>& values, const std::size_t expected, const char* field)
{
    if (values.size() != expected)
        throw std::runtime_error(std::string("Invalid OPM ") + field + " line.");
}

std::vector<ChipVoice> parseOpm(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.empty() || bytes.size() > 1024 * 1024)
        throw std::runtime_error("Invalid OPM file size.");
    if (std::find(bytes.begin(), bytes.end(), 0) != bytes.end())
        throw std::runtime_error("OPM file contains binary data.");

    const std::string text(bytes.begin(), bytes.end());
    std::istringstream lines(text);
    std::vector<ChipVoice> voices;
    ChipVoice current;
    bool active = false;
    bool haveChannel = false;
    std::array<bool, 4> haveOperators {};
    int slotMask = 0x78;

    const auto finishVoice = [&]
    {
        if (!active)
            return;
        if (!haveChannel || std::find(haveOperators.begin(), haveOperators.end(), false) != haveOperators.end())
            throw std::runtime_error("OPM voice is missing CH or operator data.");
        for (std::size_t sourceIndex = 0; sourceIndex < 4; ++sourceIndex)
        {
            static constexpr std::array<int, 4> targetOrder { 3, 1, 2, 0 };
            current.operators[static_cast<std::size_t>(targetOrder[sourceIndex])].enabled =
                (slotMask & (1 << (sourceIndex + 3))) != 0;
        }
        voices.push_back(current);
    };

    std::string line;
    while (std::getline(lines, line))
    {
        const auto comment = line.find("//");
        if (comment != std::string::npos)
            line.erase(comment);
        line = trim(line);
        if (line.empty())
            continue;

        if (line.rfind("@:", 0) == 0)
        {
            finishVoice();
            if (voices.size() >= 1024)
                throw std::runtime_error("OPM file contains too many voices.");
            current = ChipVoice {};
            haveChannel = false;
            haveOperators = {};
            slotMask = 0x78;
            active = true;
            std::istringstream header(trim(line.substr(2)));
            int number = 0;
            header >> number;
            std::getline(header, current.name);
            current.name = trim(current.name);
            continue;
        }

        if (!active)
            continue;
        const auto colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        const auto label = lowercase(trim(line.substr(0, colon)));
        const auto values = parseNumbers(line.substr(colon + 1));

        if (label == "lfo")
        {
            requireCount(values, 5, "LFO");
            requireRange(values[0], 0, 255, "LFRQ");
            requireRange(values[1], 0, 127, "AMD");
            requireRange(values[2], 0, 127, "PMD");
            requireRange(values[3], 0, 3, "WF");
            current.lfoSpeed = static_cast<int>(std::lround(values[0] * 99.0 / 255.0));
            current.ampDepth = static_cast<int>(std::lround(values[1] * 99.0 / 127.0));
            current.pitchDepth = static_cast<int>(std::lround(values[2] * 99.0 / 127.0));
            current.lfoWave = values[3];
        }
        else if (label == "ch")
        {
            requireCount(values, 8, "CH");
            requireRange(values[1], 0, 7, "Feedback");
            requireRange(values[2], 0, 7, "Algorithm");
            requireRange(values[3], 0, 3, "AMS");
            requireRange(values[4], 0, 7, "PMS");
            current.feedback = values[1];
            current.algorithm = values[2];
            current.ampSensitivity = values[3];
            current.pitchSensitivity = values[4];
            slotMask = values[5];
            haveChannel = true;
        }
        else
        {
            static const std::array<std::string, 4> labels { "m1", "c1", "m2", "c2" };
            const auto found = std::find(labels.begin(), labels.end(), label);
            if (found == labels.end())
                continue;
            requireCount(values, 11, "operator");
            const auto sourceIndex = static_cast<std::size_t>(std::distance(labels.begin(), found));
            static constexpr std::array<int, 4> targetOrder { 3, 1, 2, 0 };
            auto& op = current.operators[static_cast<std::size_t>(targetOrder[sourceIndex])];
            op.attackRate = values[0];
            op.decay1Rate = values[1];
            op.decay2Rate = values[2];
            op.releaseRate = values[3];
            op.sustainLevel = values[4];
            op.totalLevel = values[5];
            op.rateScale = values[6];
            op.multiplier = values[7];
            op.detune = decodeRegisterDetune(values[8]);
            op.detune2 = values[9];
            op.ampModEnable = values[10] != 0;
            requireRange(op.attackRate, 0, 31, "AR");
            requireRange(op.decay1Rate, 0, 31, "D1R");
            requireRange(op.decay2Rate, 0, 31, "D2R");
            requireRange(op.releaseRate, 0, 15, "RR");
            requireRange(op.sustainLevel, 0, 15, "D1L");
            requireRange(op.totalLevel, 0, 127, "TL");
            requireRange(op.rateScale, 0, 3, "KS");
            requireRange(op.multiplier, 0, 15, "MUL");
            requireRange(op.detune2, 0, 3, "DT2");
            haveOperators[sourceIndex] = true;
        }
    }
    finishVoice();
    if (voices.empty())
        throw std::runtime_error("OPM file contains no voices.");
    return voices;
}

ChipVoice parseDmp(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.empty())
        throw std::runtime_error("DMP file is empty.");

    std::size_t modeOffset = 0;
    std::size_t parameterOffset = 0;
    if (bytes[0] == 0x0a)
    {
        if (bytes.size() != 50)
            throw std::runtime_error("DMP v10 FM file must be 50 bytes.");
        modeOffset = 1;
        parameterOffset = 2;
    }
    else if (bytes[0] == 0x0b)
    {
        if (bytes.size() != 51)
            throw std::runtime_error("DMP v11 FM file must be 51 bytes.");
        modeOffset = 2;      // Version 11 inserts the target system byte at offset 1.
        parameterOffset = 3;
    }
    else
    {
        throw std::runtime_error("Unsupported DMP version.");
    }

    if (bytes[modeOffset] != 1)
        throw std::runtime_error("DMP file is not an FM instrument.");

    ChipVoice voice;
    voice.pitchSensitivity = bytes[parameterOffset];
    voice.feedback = bytes[parameterOffset + 1];
    voice.algorithm = bytes[parameterOffset + 2];
    voice.ampSensitivity = bytes[parameterOffset + 3];
    requireRange(voice.pitchSensitivity, 0, 7, "PMS/FMS");
    requireRange(voice.feedback, 0, 7, "Feedback");
    requireRange(voice.algorithm, 0, 7, "Algorithm");
    requireRange(voice.ampSensitivity, 0, 3, "AMS");

    // DMP stores Yamaha slots as OP1, OP3, OP2, OP4, matching register order.
    static constexpr std::array<int, 4> targetOrder { 3, 1, 2, 0 };
    for (std::size_t block = 0; block < 4; ++block)
    {
        const auto* data = bytes.data() + parameterOffset + 4 + block * 11;
        auto& op = voice.operators[static_cast<std::size_t>(targetOrder[block])];
        op.multiplier = data[0];
        op.totalLevel = data[1];
        op.attackRate = data[2];
        op.decay1Rate = data[3];
        op.sustainLevel = data[4];
        op.releaseRate = data[5];
        op.ampModEnable = data[6] != 0;
        op.rateScale = data[7];
        op.detune = decodeCenteredDetune(data[8] & 0x0f);
        op.detune2 = (data[8] >> 4) & 0x03;
        op.decay2Rate = data[9];
        op.ssgEg = data[10];
        requireRange(op.multiplier, 0, 15, "MUL");
        requireRange(op.totalLevel, 0, 127, "TL");
        requireRange(op.attackRate, 0, 31, "AR");
        requireRange(op.decay1Rate, 0, 31, "DR");
        requireRange(op.sustainLevel, 0, 15, "SL");
        requireRange(op.releaseRate, 0, 15, "RR");
        requireRange(op.rateScale, 0, 3, "RS");
        requireRange(op.decay2Rate, 0, 31, "D2R");
        requireRange(op.ssgEg, 0, 15, "SSG-EG");
    }
    return voice;
}
} // namespace

bool isChipVoiceExtension(const std::string& extension)
{
    const auto ext = normalizedExtension(extension);
    return ext == "opm" || ext == "tfi" || ext == "vgi" || ext == "dmp";
}

ChipVoiceImportResult importChipVoices(const std::vector<std::uint8_t>& bytes,
                                       const std::string& extension,
                                       const std::string& fallbackName)
{
    const auto ext = normalizedExtension(extension);
    ChipVoiceImportResult result;
    std::vector<ChipVoice> parsed;
    if (ext == "opm")
    {
        result.format = ChipVoiceFormat::Opm;
        parsed = parseOpm(bytes);
    }
    else if (ext == "tfi")
    {
        result.format = ChipVoiceFormat::Tfi;
        parsed.push_back(parseTfiOrVgi(bytes, false));
    }
    else if (ext == "vgi")
    {
        result.format = ChipVoiceFormat::Vgi;
        parsed.push_back(parseTfiOrVgi(bytes, true));
    }
    else if (ext == "dmp")
    {
        result.format = ChipVoiceFormat::Dmp;
        parsed.push_back(parseDmp(bytes));
    }
    else
    {
        throw std::runtime_error("Unsupported chip voice extension.");
    }

    result.voices.reserve(parsed.size());
    for (const auto& voice : parsed)
        result.voices.push_back(convertVoice(voice, fallbackName, result.warnings));
    return result;
}
} // namespace opaline
