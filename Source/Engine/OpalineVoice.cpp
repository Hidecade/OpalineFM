#include "Engine/OpalineVoice.h"

#include "Engine/OpalineTables.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace opaline
{
namespace
{
// Level, TL, and modulation-index compatibility constants.
constexpr double kCarrierLevelDbRange = 48.0;
constexpr double kModulatorIndexScale = 0.82;
constexpr double kModulatorIndexBlend = 0.05;
constexpr double kModulatorIndexExponent = 2.2;
constexpr double kOpmTlDbPerStep = 0.75;
constexpr double kOpmLogAttenuationDbPerStep = 6.020599913279624 / 256.0;
constexpr double kOpmEgIndexMax = 1023.0;
constexpr double kChipModulatorIndexScale = 0.82;
constexpr double kTypeAPhaseModulationGain = 0.90;
struct RenderModelTrim
{
    double modulatorOpalineBlend = 0.0;
    double modulatorLevelBoost = 0.0;
    double outputGain = 1.0;
};

constexpr RenderModelTrim kTypeATrim { 0.35, 1.0, 0.833 };
constexpr RenderModelTrim kTypeBChipTrim { 0.0, 0.0, 2.0 };
constexpr double kModulatorAttackSoftenSeconds = 0.003;
constexpr double kModulatorAttackInitialScale = 0.96;
constexpr double kOpmPhaseSteps = 1048576.0;
constexpr double kOpmPhaseMaxIncrement = kOpmPhaseSteps - 1.0;
constexpr double kOpmSineIndexSteps = 1024.0;
constexpr double kOpmOperatorBusPeak = 8192.0;
constexpr double kOpmBusPhaseGain = 9.0 / (kOpmOperatorBusPeak * kPi / kOpmSineIndexSteps);
constexpr double kOppTlRampSeconds = 0.012;
constexpr double kOppTlSubsteps = 8.0;
constexpr double kOppTlMax = 127.0;
constexpr double kCarrierMixGain = 0.86;
constexpr double kChipCarrierCountCompensationExponent = 0.18;
constexpr std::array<int, 5> kLevelScaleAnchors { 0, 25, 50, 75, 99 };
constexpr std::array<int, 6> kLevelScaleNoteAnchors { 36, 48, 60, 72, 84, 96 };
constexpr std::array<std::array<int, 6>, 5> kMeasuredLevelScaleOffsets {{
    {{ 0, 0, 0, 0, 0, 0 }},
    {{ 0, 1, 2, 4, 8, 16 }},
    {{ 1, 2, 4, 8, 16, 33 }},
    {{ 1, 3, 6, 12, 25, 50 }},
    {{ 1, 3, 7, 16, 33, 67 }}
}};
constexpr std::array<double, 8> kOpalinePitchSensitivitySemitones {
    2.0,    // PMS=0 uses the compatible vibrato oscillator path; measured like PMS=5.
    0.125,
    0.25,
    0.5,
    1.0,
    2.0,
    4.0,
    7.0
};
constexpr std::array<double, 8> kOpmPitchSensitivitySemitones {
    1.0,    // PMS=0 uses the compatible VIBRATO OSC path with PMS=5 depth.
    0.05,
    0.10,
    0.20,
    0.50,
    1.00,
    4.00,
    7.00
};

int nextChipNoiseInjectedBit(std::uint32_t& noiseLfsr, int& noiseBit)
{
    const int injected = noiseLfsr & 1u;
    const bool reset = (noiseLfsr & 0xffffu) == 0u && noiseBit == 0;
    const int feedback = static_cast<int>((noiseLfsr >> 2) & 1u) ^ noiseBit;
    const int newBit = reset ? 1 : feedback;
    noiseBit = static_cast<int>(noiseLfsr & 1u);
    noiseLfsr >>= 1;
    noiseLfsr |= static_cast<std::uint32_t>(newBit) << 15;
    return injected;
}

int nextChipSampleAndHoldValue(std::uint32_t& noiseLfsr, int& noiseBit)
{
    int value = 0;
    for (int bit = 0; bit < 8; ++bit)
        value = (value << 1) | nextChipNoiseInjectedBit(noiseLfsr, noiseBit);

    return value;
}

double outputLevelToCarrierAmplitude(const double level)
{
    const double normalized = clampDouble(level, 0.0, 99.0) / 99.0;
    if (normalized <= 0.0)
        return 0.0;
    return std::pow(10.0, -((1.0 - normalized) * kCarrierLevelDbRange) / 20.0);
}

double amplitudeToDb(const double amplitude)
{
    return -20.0 * std::log10(std::max(amplitude, 1.0e-9));
}

double amplitudeFactorToDbOffset(const double factor)
{
    return -20.0 * std::log10(std::max(factor, 1.0e-9));
}

double dbToAmplitude(const double db)
{
    return std::pow(10.0, -db / 20.0);
}

double opalineLevelToOpmTl(const double level)
{
    // Map compatible LEVEL 0..99 to OPM TL 127..0.
    return std::round((1.0 - clampDouble(level, 0.0, 99.0) / 99.0) * kOppTlMax);
}

double opmTlToDb(const double tl)
{
    return clampDouble(tl, 0.0, kOppTlMax) * kOpmTlDbPerStep;
}

double opmTlToAmplitude(const double tl)
{
    return dbToAmplitude(opmTlToDb(tl));
}

double outputLevelToCarrierAmplitudeChipLike(const double level)
{
    return opmTlToAmplitude(opalineLevelToOpmTl(level));
}

double outputLevelToModulatorIndex(const double level)
{
    const double carrierAmp = outputLevelToCarrierAmplitude(level);
    const double normalized = clampDouble(level, 0.0, 99.0) / 99.0;
    if (normalized <= 0.0)
        return 0.0;

    const double shapedIndex = std::pow(normalized, kModulatorIndexExponent);
    return carrierAmp * kModulatorIndexScale + shapedIndex * kModulatorIndexBlend;
}

double outputLevelToModulatorIndexChipLike(const double level)
{
    return outputLevelToCarrierAmplitudeChipLike(level) * kChipModulatorIndexScale;
}

double measuredOffsetAtNoteAnchor(const int levelScale, const std::size_t noteIndex)
{
    const int scale = clampInt(levelScale, kLevelScaleAnchors.front(), kLevelScaleAnchors.back());
    for (std::size_t upper = 1; upper < kLevelScaleAnchors.size(); ++upper)
    {
        if (scale > kLevelScaleAnchors[upper])
            continue;

        const std::size_t lower = upper - 1;
        const double range = static_cast<double>(kLevelScaleAnchors[upper] - kLevelScaleAnchors[lower]);
        const double amount = static_cast<double>(scale - kLevelScaleAnchors[lower]) / range;
        const double low = static_cast<double>(kMeasuredLevelScaleOffsets[lower][noteIndex]);
        const double high = static_cast<double>(kMeasuredLevelScaleOffsets[upper][noteIndex]);
        return low + (high - low) * amount;
    }

    return static_cast<double>(kMeasuredLevelScaleOffsets.back()[noteIndex]);
}

int keyboardScaledTargetLevel(const int level, const int levelScale, const int note)
{
    const int scaleAmount = keyboardLevelScaleOffset(note, levelScale);
    return clampInt(level - scaleAmount, 0, 99);
}

double operatorAmpModFactor(const double depth, const double shapeValue)
{
    return clampDouble(1.0 - depth * shapeValue, 0.0, 1.0);
}

double modulatorAttackSoftening(const double age)
{
    const double progress = clampDouble(age / kModulatorAttackSoftenSeconds, 0.0, 1.0);
    return kModulatorAttackInitialScale + (1.0 - kModulatorAttackInitialScale) * progress;
}

double modulatorAttackSofteningForModel(const double age, const OpalineRenderModel renderModel)
{
    (void) renderModel;
    return modulatorAttackSoftening(age);
}

double envelopeAmpForModel(const double envelopeAmp, const OpalineRenderModel renderModel)
{
    (void) renderModel;
    return envelopeAmp;
}

double operatorVelocityFactor(const int opVelocity, const int noteVelocity, const bool carrier)
{
    const double amount = clampDouble(static_cast<double>(opVelocity), 0.0, 7.0) / 7.0;
    if (amount <= 0.0)
        return 1.0;

    const double velocity = clampDouble(static_cast<double>(noteVelocity), 1.0, 127.0) / 127.0;
    const double shapedVelocity = std::pow(velocity, 1.35);
    const double minimum = carrier ? 0.20 : 0.32;
    const double maximum = carrier ? 1.45 : 1.65;
    const double fullRangeFactor = minimum + (maximum - minimum) * shapedVelocity;
    return 1.0 + (fullRangeFactor - 1.0) * amount;
}

double opmStylePhaseAdvance(const double frequency, const double sampleRate)
{
    const double increment = std::round(clampDouble(frequency, 0.0, sampleRate * 0.49) * kOpmPhaseSteps / sampleRate);
    return clampDouble(increment, 0.0, kOpmPhaseMaxIncrement) * 2.0 * kPi / kOpmPhaseSteps;
}

double opmPhaseBusToRadians(const double bus)
{
    return bus * kPi / kOpmSineIndexSteps * kOpmBusPhaseGain;
}

bool usesSharedTypeABase(const OpalineRenderModel renderModel)
{
    return renderModel == OpalineRenderModel::TypeA
        || renderModel == OpalineRenderModel::TypeB;
}

bool usesChipOperatorPath(const OpalineRenderModel renderModel)
{
    return renderModel == OpalineRenderModel::TypeB;
}

const RenderModelTrim& trimForModel(const OpalineRenderModel renderModel)
{
    return renderModel == OpalineRenderModel::TypeB ? kTypeBChipTrim : kTypeATrim;
}

double opmPhaseBusToRadians(const double bus, const OpalineRenderModel renderModel)
{
    if (usesSharedTypeABase(renderModel))
    {
        const double phaseIndex = std::round(bus);
        return phaseIndex * 2.0 * kPi / kOpmSineIndexSteps;
    }

    return opmPhaseBusToRadians(bus);
}

int chipOperatorPhaseIndex(const double phase)
{
    if (!std::isfinite(phase))
        return 0;

    const double cycle = phase / (2.0 * kPi);
    const double wrapped = cycle - std::floor(cycle);
    return static_cast<int>(std::floor(wrapped * kOpmSineIndexSteps))
        & (static_cast<int>(kOpmSineIndexSteps) - 1);
}

int chipPhaseOffsetIndexFromRadians(const double radians)
{
    return static_cast<int>(std::round(radians * kOpmSineIndexSteps / (2.0 * kPi)));
}

int chipOperatorPhaseIndex(const double phase, const double modulationBus, const double feedbackRadians)
{
    // Combine the base phase, PM bus, and feedback in the 1024-step domain.
    const int baseIndex = chipOperatorPhaseIndex(phase);
    const int modulationIndex = chipPhaseOffsetIndexFromRadians(opmPhaseBusToRadians(modulationBus, OpalineRenderModel::TypeB));
    const int feedbackIndex = chipPhaseOffsetIndexFromRadians(feedbackRadians);
    return (baseIndex + modulationIndex + feedbackIndex) & (static_cast<int>(kOpmSineIndexSteps) - 1);
}

int chipOperatorSign(const int phaseIndex)
{
    return (phaseIndex & 512) != 0 ? -1 : 1;
}

int chipOperatorLogSineAttenuation(const int phaseIndex)
{
    static constexpr std::array<int, 256> kLogSinRom {
        0x859, 0x6c3, 0x607, 0x58b, 0x52e, 0x4e4, 0x4a6, 0x471,
        0x443, 0x41a, 0x3f5, 0x3d3, 0x3b5, 0x398, 0x37e, 0x365,
        0x34e, 0x339, 0x324, 0x311, 0x2ff, 0x2ed, 0x2dc, 0x2cd,
        0x2bd, 0x2af, 0x2a0, 0x293, 0x286, 0x279, 0x26d, 0x261,
        0x256, 0x24b, 0x240, 0x236, 0x22c, 0x222, 0x218, 0x20f,
        0x206, 0x1fd, 0x1f5, 0x1ec, 0x1e4, 0x1dc, 0x1d4, 0x1cd,
        0x1c5, 0x1be, 0x1b7, 0x1b0, 0x1a9, 0x1a2, 0x19b, 0x195,
        0x18f, 0x188, 0x182, 0x17c, 0x177, 0x171, 0x16b, 0x166,
        0x160, 0x15b, 0x155, 0x150, 0x14b, 0x146, 0x141, 0x13c,
        0x137, 0x133, 0x12e, 0x129, 0x125, 0x121, 0x11c, 0x118,
        0x114, 0x10f, 0x10b, 0x107, 0x103, 0x0ff, 0x0fb, 0x0f8,
        0x0f4, 0x0f0, 0x0ec, 0x0e9, 0x0e5, 0x0e2, 0x0de, 0x0db,
        0x0d7, 0x0d4, 0x0d1, 0x0cd, 0x0ca, 0x0c7, 0x0c4, 0x0c1,
        0x0be, 0x0bb, 0x0b8, 0x0b5, 0x0b2, 0x0af, 0x0ac, 0x0a9,
        0x0a7, 0x0a4, 0x0a1, 0x09f, 0x09c, 0x099, 0x097, 0x094,
        0x092, 0x08f, 0x08d, 0x08a, 0x088, 0x086, 0x083, 0x081,
        0x07f, 0x07d, 0x07a, 0x078, 0x076, 0x074, 0x072, 0x070,
        0x06e, 0x06c, 0x06a, 0x068, 0x066, 0x064, 0x062, 0x060,
        0x05e, 0x05c, 0x05b, 0x059, 0x057, 0x055, 0x053, 0x052,
        0x050, 0x04e, 0x04d, 0x04b, 0x04a, 0x048, 0x046, 0x045,
        0x043, 0x042, 0x040, 0x03f, 0x03e, 0x03c, 0x03b, 0x039,
        0x038, 0x037, 0x035, 0x034, 0x033, 0x031, 0x030, 0x02f,
        0x02e, 0x02d, 0x02b, 0x02a, 0x029, 0x028, 0x027, 0x026,
        0x025, 0x024, 0x023, 0x022, 0x021, 0x020, 0x01f, 0x01e,
        0x01d, 0x01c, 0x01b, 0x01a, 0x019, 0x018, 0x017, 0x017,
        0x016, 0x015, 0x014, 0x014, 0x013, 0x012, 0x011, 0x011,
        0x010, 0x00f, 0x00f, 0x00e, 0x00d, 0x00d, 0x00c, 0x00c,
        0x00b, 0x00a, 0x00a, 0x009, 0x009, 0x008, 0x008, 0x007,
        0x007, 0x007, 0x006, 0x006, 0x005, 0x005, 0x005, 0x004,
        0x004, 0x004, 0x003, 0x003, 0x003, 0x002, 0x002, 0x002,
        0x002, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001,
        0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
    };

    int quarterIndex = phaseIndex & 255;
    if ((phaseIndex & 256) != 0)
        quarterIndex ^= 255;

    return kLogSinRom[static_cast<std::size_t>(quarterIndex)];
}

double quantizeOperatorAudioForModel(const double audio, const OpalineRenderModel renderModel)
{
    (void) renderModel;
    return audio;
}

double chipExpOutputFromAttenuation(const int attenuation)
{
    static constexpr std::array<int, 256> kExpRom {
        0x7fa, 0x7f5, 0x7ef, 0x7ea, 0x7e4, 0x7df, 0x7da, 0x7d4,
        0x7cf, 0x7c9, 0x7c4, 0x7bf, 0x7b9, 0x7b4, 0x7ae, 0x7a9,
        0x7a4, 0x79f, 0x799, 0x794, 0x78f, 0x78a, 0x784, 0x77f,
        0x77a, 0x775, 0x770, 0x76a, 0x765, 0x760, 0x75b, 0x756,
        0x751, 0x74c, 0x747, 0x742, 0x73d, 0x738, 0x733, 0x72e,
        0x729, 0x724, 0x71f, 0x71a, 0x715, 0x710, 0x70b, 0x706,
        0x702, 0x6fd, 0x6f8, 0x6f3, 0x6ee, 0x6e9, 0x6e5, 0x6e0,
        0x6db, 0x6d6, 0x6d2, 0x6cd, 0x6c8, 0x6c4, 0x6bf, 0x6ba,
        0x6b5, 0x6b1, 0x6ac, 0x6a8, 0x6a3, 0x69e, 0x69a, 0x695,
        0x691, 0x68c, 0x688, 0x683, 0x67f, 0x67a, 0x676, 0x671,
        0x66d, 0x668, 0x664, 0x65f, 0x65b, 0x657, 0x652, 0x64e,
        0x649, 0x645, 0x641, 0x63c, 0x638, 0x634, 0x630, 0x62b,
        0x627, 0x623, 0x61e, 0x61a, 0x616, 0x612, 0x60e, 0x609,
        0x605, 0x601, 0x5fd, 0x5f9, 0x5f5, 0x5f0, 0x5ec, 0x5e8,
        0x5e4, 0x5e0, 0x5dc, 0x5d8, 0x5d4, 0x5d0, 0x5cc, 0x5c8,
        0x5c4, 0x5c0, 0x5bc, 0x5b8, 0x5b4, 0x5b0, 0x5ac, 0x5a8,
        0x5a4, 0x5a0, 0x59c, 0x599, 0x595, 0x591, 0x58d, 0x589,
        0x585, 0x581, 0x57e, 0x57a, 0x576, 0x572, 0x56f, 0x56b,
        0x567, 0x563, 0x560, 0x55c, 0x558, 0x554, 0x551, 0x54d,
        0x549, 0x546, 0x542, 0x53e, 0x53b, 0x537, 0x534, 0x530,
        0x52c, 0x529, 0x525, 0x522, 0x51e, 0x51b, 0x517, 0x514,
        0x510, 0x50c, 0x509, 0x506, 0x502, 0x4ff, 0x4fb, 0x4f8,
        0x4f4, 0x4f1, 0x4ed, 0x4ea, 0x4e7, 0x4e3, 0x4e0, 0x4dc,
        0x4d9, 0x4d6, 0x4d2, 0x4cf, 0x4cc, 0x4c8, 0x4c5, 0x4c2,
        0x4be, 0x4bb, 0x4b8, 0x4b5, 0x4b1, 0x4ae, 0x4ab, 0x4a8,
        0x4a4, 0x4a1, 0x49e, 0x49b, 0x498, 0x494, 0x491, 0x48e,
        0x48b, 0x488, 0x485, 0x482, 0x47e, 0x47b, 0x478, 0x475,
        0x472, 0x46f, 0x46c, 0x469, 0x466, 0x463, 0x460, 0x45d,
        0x45a, 0x457, 0x454, 0x451, 0x44e, 0x44b, 0x448, 0x445,
        0x442, 0x43f, 0x43c, 0x439, 0x436, 0x433, 0x430, 0x42d,
        0x42a, 0x428, 0x425, 0x422, 0x41f, 0x41c, 0x419, 0x416,
        0x414, 0x411, 0x40e, 0x40b, 0x408, 0x406, 0x403, 0x400
    };

    const int atten = clampInt(attenuation, 0, 4095);
    const int output = (kExpRom[static_cast<std::size_t>(atten & 255)] << 2) >> (atten >> 8);
    return static_cast<double>(output) / 8192.0;
}

double chipOperatorOutputFromAttenuationIndex(const int phaseIndex, const double attenuationIndex)
{
    const int waveAttenuation = chipOperatorLogSineAttenuation(phaseIndex);
    const int egAttenuation = clampInt(static_cast<int>(std::round(attenuationIndex)), 0, 1023) << 2;
    const int attenuation = clampInt(waveAttenuation + egAttenuation, 0, 4095);
    const double output = static_cast<double>(chipOperatorSign(phaseIndex)) * chipExpOutputFromAttenuation(attenuation);
    return quantizeOperatorAudioForModel(output, OpalineRenderModel::TypeB);
}

double quantizeOperatorBusForModel(const double bus, const OpalineRenderModel renderModel)
{
    if (usesSharedTypeABase(renderModel))
        return clampDouble(std::round(bus), -kOpmOperatorBusPeak, kOpmOperatorBusPeak - 1.0);

    return bus;
}

int chipBusInteger(const double bus)
{
    return clampInt(static_cast<int>(std::round(bus)), -32768, 32767);
}

double chipArithmeticShiftRightOne(const double bus)
{
    const int value = chipBusInteger(bus);
    return static_cast<double>(value >= 0 ? value / 2 : -(((-value) + 1) / 2));
}

double mixPhaseModulationForModel(const double sum, const int inputCount, const OpalineRenderModel renderModel)
{
    if (renderModel == OpalineRenderModel::TypeB && inputCount > 0)
        return quantizeOperatorBusForModel(chipArithmeticShiftRightOne(sum), renderModel);

    if (usesSharedTypeABase(renderModel) && inputCount > 0)
        return quantizeOperatorBusForModel(sum * 0.5, renderModel);

    return sum;
}

double quantizeVoiceOutputForModel(const double sample, const OpalineRenderModel renderModel)
{
    if (renderModel == OpalineRenderModel::TypeB)
        return std::round(sample * 32768.0) / 32768.0;

    return sample;
}

int opmDacQuantizeMix(const int input)
{
    const int mix = clampInt(input, -32768, 32767);
    const int magnitude = std::abs(mix);
    int exponent = 1;
    while (exponent < 7 && magnitude >= (512 << exponent))
        ++exponent;

    const int step = 1 << (exponent - 1);
    const int quantizedMagnitude = (magnitude / step) * step;
    return mix < 0 ? -quantizedMagnitude : quantizedMagnitude;
}

double chipMixerOutputFromBus(const double bus)
{
    const int accumulated = clampInt(static_cast<int>(std::round(bus)), -131072, 131071);
    const int dacOutput = opmDacQuantizeMix(accumulated);
    return static_cast<double>(dacOutput) / (kOpmOperatorBusPeak * 2.0);
}

double feedbackHistoryBusForModel(const std::array<double, 2>& history, const OpalineRenderModel renderModel)
{
    const double sum = history[0] + history[1];
    if (renderModel == OpalineRenderModel::TypeB)
        return chipArithmeticShiftRightOne(sum);

    if (usesSharedTypeABase(renderModel))
        return sum * 0.5;

    return sum;
}

double opmFeedbackBusToRadians(const double bus,
                               const int level,
                               const OpalineRenderModel renderModel,
                               const double sharedDivisor)
{
    const int feedback = clampInt(level, 0, 7);
    if (feedback <= 0)
        return 0.0;

    if (usesSharedTypeABase(renderModel))
    {
        const double phaseIndex = quantizeOperatorBusForModel(bus, renderModel)
            / sharedDivisor;
        return phaseIndex * 2.0 * kPi / kOpmSineIndexSteps;
    }

    const int shift = feedback + 6;
    return bus * std::pow(2.0, static_cast<double>(shift)) * 2.0 * kPi
        / (kOpmSineIndexSteps * 65536.0) * kOpmBusPhaseGain;
}

double levelToOppTlTarget(const double level, const OpalineRenderModel renderModel)
{
    const double clampedLevel = clampDouble(level, 0.0, 99.0);
    if (renderModel == OpalineRenderModel::TypeB)
    {
        if (clampedLevel <= 0.0)
            return kOppTlMax;

        return std::round(99.0 - clampedLevel);
    }

    return std::round((1.0 - clampedLevel / 99.0) * kOppTlMax);
}

double oppTlUnitsToLevel(const double units)
{
    return clampDouble((1.0 - clampDouble(units, 0.0, kOppTlMax * kOppTlSubsteps)
                            / (kOppTlMax * kOppTlSubsteps))
                           * 99.0,
                       0.0,
                       99.0);
}

double steppedModDepth(const double normalized, const std::array<double, 9>& table)
{
    if (normalized <= 0.0)
        return 0.0;

    const double scaled = normalized * static_cast<double>(table.size() - 1);
    const auto index = static_cast<std::size_t>(std::floor(scaled));
    const double fraction = scaled - std::floor(scaled);
    const double lower = table[std::min(index, table.size() - 1)];
    const double upper = table[std::min(index + 1, table.size() - 1)];
    return lower + (upper - lower) * std::pow(fraction, 1.35);
}

double opmStylePitchModDepth(const int depth, const int sensitivity)
{
    const double normalized = static_cast<double>(clampInt(depth, 0, 99)) / 99.0;
    const auto sensitivityIndex = static_cast<std::size_t>(clampInt(sensitivity, 0, 7));
    return normalized * kOpalinePitchSensitivitySemitones[sensitivityIndex];
}

double chipStylePitchModDepth(const int depth, const int sensitivity)
{
    const double normalized = static_cast<double>(clampInt(depth, 0, 99)) / 99.0;
    const auto sensitivityIndex = static_cast<std::size_t>(clampInt(sensitivity, 0, 7));
    return normalized * kOpmPitchSensitivitySemitones[sensitivityIndex];
}

double pitchModDepthForModel(const int depth, const int sensitivity, const int wave, const OpalineRenderModel renderModel)
{
    if (clampInt(wave, 0, 3) == 1 && clampInt(sensitivity, 0, 7) == 7)
        return static_cast<double>(clampInt(depth, 0, 99)) / 99.0 * 8.0;

    if (usesSharedTypeABase(renderModel))
        return chipStylePitchModDepth(depth, sensitivity);

    return opmStylePitchModDepth(depth, sensitivity);
}

double modWheelPitchDepthForModel(const int range, const int sensitivity, const int wave, const OpalineRenderModel renderModel)
{
    return pitchModDepthForModel(range, sensitivity, wave, renderModel);
}

double opmStyleAmpModDepth(const int depth, const int sensitivity)
{
    static constexpr std::array<double, 9> kDepthTable { 0.0, 0.025, 0.055, 0.11, 0.22, 0.38, 0.58, 0.78, 0.96 };
    const double normalized = static_cast<double>(clampInt(depth, 0, 99)) / 99.0;
    const double normalizedSensitivity = static_cast<double>(clampInt(sensitivity, 0, 3)) / 3.0;
    return normalizedSensitivity * steppedModDepth(normalized, kDepthTable);
}

struct LfoDelayTiming
{
    double waitSeconds = 0.0;
    double fadeSeconds = 0.0;
};

LfoDelayTiming lfoDelayTimingForValue(const int delay)
{
    if (delay <= 0)
        return {};

    const double waitSeconds = 0.25 * std::pow(2.0, static_cast<double>(clampInt(delay, 0, 99)) / 25.0);
    const double highDelay = clampDouble((static_cast<double>(delay) - 75.0) / 24.0, 0.0, 1.0);
    const double fadeSeconds = waitSeconds * (1.0 + 2.0 * highDelay);
    return { waitSeconds, fadeSeconds };
}

double lfoDelayFactor(const int delay, const double age, const double waitSeconds, const double fadeSeconds)
{
    if (delay <= 0)
        return 1.0;

    if (age <= waitSeconds)
        return 0.0;

    return std::min(1.0, (age - waitSeconds) / std::max(0.001, fadeSeconds));
}

std::pair<double, double> opalineLfoShape(const double phase, const int wave)
{
    const double cycle = phase - std::floor(phase);
    const int index = static_cast<int>(std::floor(cycle * 256.0)) & 255;
    double am = 0.0;
    double pm = 0.0;

    if (wave == 0) // SAW UP
    {
        am = static_cast<double>(255 - index) / 255.0;
        pm = index < 128 ? static_cast<double>(index) / 127.0
                         : static_cast<double>(index - 255) / 127.0;
    }
    else if (wave == 1) // SQUARE
    {
        am = index < 128 ? 1.0 : 0.0;
        pm = index < 128 ? 1.0 : -1.0;
    }
    else if (wave == 2) // TRIANGLE
    {
        const double amRaw = index < 128 ? 255.0 - static_cast<double>(index) * 2.0
                                         : static_cast<double>(index) * 2.0 - 256.0;
        am = amRaw / 255.0;
        const double pitchPhase = static_cast<double>(index) / 256.0;
        if (pitchPhase < 0.25)
            pm = pitchPhase * 4.0;
        else if (pitchPhase < 0.75)
            pm = 2.0 - pitchPhase * 4.0;
        else
            pm = pitchPhase * 4.0 - 4.0;
    }
    else // S/H
    {
        am = 0.5;
        pm = 0.0;
    }

    return { clampDouble(am, 0.0, 1.0), clampDouble(pm, -1.0, 1.0) };
}

}

int keyboardLevelScaleOffset(const int midiNote, const int levelScale)
{
    const int note = clampInt(midiNote, 0, 127);
    const auto quantize = [](const double value)
    {
        return clampInt(static_cast<int>(std::floor(value + 0.5)), 0, static_cast<int>(kOppTlMax));
    };

    if (note < kLevelScaleNoteAnchors.front())
    {
        const double anchor = measuredOffsetAtNoteAnchor(levelScale, 0);
        const double octaveFactor = std::pow(2.0,
            static_cast<double>(note - kLevelScaleNoteAnchors.front()) / 12.0);
        return clampInt(static_cast<int>(std::floor(anchor * octaveFactor)), 0,
                        static_cast<int>(kOppTlMax));
    }

    for (std::size_t upper = 1; upper < kLevelScaleNoteAnchors.size(); ++upper)
    {
        if (note > kLevelScaleNoteAnchors[upper])
            continue;

        const std::size_t lower = upper - 1;
        const int lowNote = kLevelScaleNoteAnchors[lower];
        const double octavePosition = static_cast<double>(note - lowNote) / 12.0;
        const double amount = std::pow(2.0, octavePosition) - 1.0;
        const double low = measuredOffsetAtNoteAnchor(levelScale, lower);
        const double high = measuredOffsetAtNoteAnchor(levelScale, upper);
        return quantize(low + (high - low) * amount);
    }

    const double anchor = measuredOffsetAtNoteAnchor(levelScale, kLevelScaleNoteAnchors.size() - 1);
    const double octaveFactor = std::pow(2.0,
        static_cast<double>(note - kLevelScaleNoteAnchors.back()) / 12.0);
    return quantize(anchor * octaveFactor);
}

void OpalineVoice::start(const OpalinePatch& patch, const int newMidiNote, const int velocity,
                         const double sampleRate, const OpalineRenderModel renderModel,
                         const int portamentoFromNote, const double portamentoSeconds)
{
    midiNote = newMidiNote;
    noteVelocity = clampInt(velocity, 0, 127);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    ageSeconds = 0.0;
    portamentoOffsetSemitones = portamentoFromNote >= 0
        ? static_cast<double>(portamentoFromNote - newMidiNote)
        : 0.0;
    const double portamentoSamples = portamentoSeconds > 0.0
        ? portamentoSeconds * currentSampleRate
        : 0.0;
    portamentoStepPerSample = portamentoSamples > 0.0
        ? std::abs(portamentoOffsetSemitones) / portamentoSamples
        : 0.0;
    operatorTlStepInterval = std::max(1.0, currentSampleRate * kOppTlRampSeconds / (kOppTlMax * kOppTlSubsteps));
    phases.fill(0.0);
    operatorTlAccumulators.fill(0.0);
    delayedPitchLfo = 0.0;
    sampleAndHoldLfsr = 0xace1u ^ (static_cast<std::uint32_t>(midiNote & 0x7f) << 8)
        ^ static_cast<std::uint32_t>(noteVelocity & 0x7f);
    sampleAndHoldBit = static_cast<int>((sampleAndHoldLfsr >> 1) & 1u);
    sampleAndHoldCycle = -1;
    sampleAndHoldSubcycle = -1;
    sampleAndHoldValue = nextChipSampleAndHoldValue(sampleAndHoldLfsr, sampleAndHoldBit);
    sampleAndHoldShiftRegister = static_cast<std::uint16_t>(sampleAndHoldValue << 8);
    feedbackHistory.fill(0.0);
    failed = false;
    activeRenderModel = renderModel;
    pitchEnvelope.reset(currentSampleRate);
    pitchEnvelope.noteOn(patch.pitchEnvelope);

    for (int sensitivity = 0; sensitivity <= 7; ++sensitivity)
    {
        const auto index = static_cast<std::size_t>(sensitivity);
        carrierVelocityDb[index] = amplitudeFactorToDbOffset(
            operatorVelocityFactor(sensitivity, noteVelocity, true));
        modulatorVelocityDb[index] = amplitudeFactorToDbOffset(
            operatorVelocityFactor(sensitivity, noteVelocity, false));
    }

    const int scalingNote = midiNote + patch.transpose;
    for (int i = 0; i < kOperatorCount; ++i)
    {
        const auto& op = patch.operators[static_cast<std::size_t>(i)];
        const int targetLevel = renderModel == OpalineRenderModel::TypeB
            ? keyboardScaledTargetLevel(op.level, op.levelScale, scalingNote)
            : op.level;
        operatorOppTlUnits[static_cast<std::size_t>(i)] =
            levelToOppTlTarget(targetLevel, renderModel) * kOppTlSubsteps;
        envelopes[static_cast<std::size_t>(i)].reset(currentSampleRate);
        envelopes[static_cast<std::size_t>(i)].noteOn(patch.operators[static_cast<std::size_t>(i)].envelope,
                                                      patch.operators[static_cast<std::size_t>(i)].rateScale,
                                                      scalingNote);
        chipEnvelopes[static_cast<std::size_t>(i)].reset(currentSampleRate);
        chipEnvelopes[static_cast<std::size_t>(i)].noteOn(patch.operators[static_cast<std::size_t>(i)].envelope,
                                                          patch.operators[static_cast<std::size_t>(i)].rateScale,
                                                          scalingNote);
    }
}

void OpalineVoice::retargetPitch(const int newMidiNote, const double portamentoSeconds)
{
    const double currentNote = static_cast<double>(midiNote) + portamentoOffsetSemitones;
    midiNote = clampInt(newMidiNote, 0, 127);
    portamentoOffsetSemitones = currentNote - static_cast<double>(midiNote);

    const double portamentoSamples = portamentoSeconds > 0.0
        ? portamentoSeconds * currentSampleRate
        : 0.0;
    portamentoStepPerSample = portamentoSamples > 0.0
        ? std::abs(portamentoOffsetSemitones) / portamentoSamples
        : 0.0;
}

void OpalineVoice::release()
{
    for (auto& envelope : envelopes)
        envelope.noteOff();
    for (auto& envelope : chipEnvelopes)
        envelope.noteOff();
    pitchEnvelope.noteOff();
}

bool OpalineVoice::isActive() const
{
    if (failed)
        return false;

    if (activeRenderModel == OpalineRenderModel::TypeB)
    {
        for (const auto& envelope : chipEnvelopes)
        {
            if (envelope.isActive())
                return true;
        }
        return false;
    }

    for (const auto& envelope : envelopes)
    {
        if (envelope.isActive())
            return true;
    }
    return false;
}

double OpalineVoice::nextOperatorLevel(const int index, const int targetLevel)
{
    return oppTlUnitsToLevel(nextOperatorTl(index, targetLevel) * kOppTlSubsteps);
}

double OpalineVoice::nextOperatorTl(const int index, const int targetLevel)
{
    const auto opSize = static_cast<std::size_t>(index);
    double current = operatorOppTlUnits[opSize];
    const double target = levelToOppTlTarget(targetLevel, activeRenderModel);
    operatorTlAccumulators[opSize] += 1.0;
    const bool match = operatorTlAccumulators[opSize] >= operatorTlStepInterval;
    bool stepped = false;

    if (match)
    {
        const double value = std::floor(current / kOppTlSubsteps);
        if (value < target)
        {
            current += 1.0;
            stepped = true;
        }
        else if (value > target)
        {
            current -= 1.0;
            stepped = true;
        }
        else
        {
            current = target * kOppTlSubsteps;
            operatorTlAccumulators[opSize] = 0.0;
        }
    }

    if (match && stepped)
        operatorTlAccumulators[opSize] -= operatorTlStepInterval;

    operatorOppTlUnits[opSize] = clampDouble(current, 0.0, kOppTlMax * kOppTlSubsteps);
    return operatorOppTlUnits[opSize] / kOppTlSubsteps;
}

double OpalineVoice::nextPitchModulation(const double pitchLfo)
{
    const double delayed = delayedPitchLfo;
    delayedPitchLfo = pitchLfo;
    return delayed;
}

std::pair<double, double> OpalineVoice::nextSampleAndHoldLfoShape(const double phase)
{
    const int subcycle = static_cast<int>(std::floor(phase * 16.0));
    if (sampleAndHoldSubcycle < 0 || subcycle < sampleAndHoldSubcycle)
        sampleAndHoldSubcycle = subcycle - 1;

    int steps = clampInt(subcycle - sampleAndHoldSubcycle, 0, 64);
    while (steps-- > 0)
    {
        ++sampleAndHoldSubcycle;
        const bool lfoClock = (sampleAndHoldSubcycle & 15) == 0;
        const int feedback = static_cast<int>(((sampleAndHoldShiftRegister >> 15) ^ (sampleAndHoldShiftRegister >> 13)) & 1u);
        const int bit = lfoClock ? nextChipNoiseInjectedBit(sampleAndHoldLfsr, sampleAndHoldBit) : feedback;
        sampleAndHoldShiftRegister = static_cast<std::uint16_t>((sampleAndHoldShiftRegister << 1) | static_cast<std::uint16_t>(bit));
    }

    sampleAndHoldCycle = static_cast<int>(std::floor(phase));
    sampleAndHoldValue = static_cast<int>((sampleAndHoldShiftRegister >> 8) & 0xffu);
    const double am = static_cast<double>(sampleAndHoldValue) / 255.0;
    const double pm = static_cast<double>(sampleAndHoldValue) / 127.5 - 1.0;
    return { clampDouble(am, 0.0, 1.0), clampDouble(pm, -1.0, 1.0) };
}

OperatorRender OpalineVoice::renderOperator(const int opIndex,
                                         const OpalinePatch& patch,
                                         const Algorithm& algorithm,
                                         const double baseFrequency,
                                         const double ampDepth,
                                         const double lfoAm,
                                         const OpalineRenderModel renderModel,
                                         std::array<bool, kOperatorCount>& computed,
                                         std::array<OperatorRender, kOperatorCount>& outputs)
{
    const auto opSize = static_cast<std::size_t>(opIndex);
    if (computed[opSize])
        return outputs[opSize];

    const auto& op = patch.operators[opSize];
    if (!op.enabled)
    {
        computed[opSize] = true;
        outputs[opSize] = {};
        return {};
    }

    double phaseModulation = 0.0;
    for (int i = 0; i < algorithm.depCounts[opSize]; ++i)
    {
        const int dep = algorithm.deps[opSize][static_cast<std::size_t>(i)];
        phaseModulation += renderOperator(dep,
                                          patch,
                                          algorithm,
                                          baseFrequency,
                                          ampDepth,
                                          lfoAm,
                                          renderModel,
                                          computed,
                                          outputs)
                               .modulation;
    }
    if (renderModel == OpalineRenderModel::TypeA)
        phaseModulation *= kTypeAPhaseModulationGain;

    phaseModulation = mixPhaseModulationForModel(phaseModulation, algorithm.depCounts[opSize], renderModel);

    const double feedback = opIndex == 3
        ? opmFeedbackBusToRadians(feedbackHistoryBusForModel(feedbackHistory, renderModel),
                                  patch.feedback,
                                  renderModel,
                                  cachedFeedbackDivisor)
        : 0.0;

    const double ratio = opalineRatios()[static_cast<std::size_t>(op.ratioIndex)];
    const int oscillatorNote = midiNote + patch.transpose;
    const double frequency = baseFrequency * ratio
        + opmStyleDt1FrequencyOffset(baseFrequency, ratio, op.detune, oscillatorNote);
    phases[opSize] += opmStylePhaseAdvance(frequency, currentSampleRate);
    if (phases[opSize] > 2.0 * kPi)
        phases[opSize] -= 2.0 * kPi;

    double chipEnvelopeIndex = 0.0;
    double envelopeAmp = 0.0;
    if (usesChipOperatorPath(renderModel))
    {
        chipEnvelopeIndex = chipEnvelopes[opSize].nextIndex();
    }
    else
    {
        envelopeAmp = envelopeAmpForModel(envelopes[opSize].next(), renderModel);
    }
    const bool carrier = operatorCarrierRoles[opSize];
    auto& scaleCache = operatorScaleCaches[opSize];
    if (scaleCache.midiNote != oscillatorNote || scaleCache.levelScale != op.levelScale)
    {
        scaleCache.midiNote = oscillatorNote;
        scaleCache.levelScale = op.levelScale;
        scaleCache.tlOffset = static_cast<double>(keyboardLevelScaleOffset(oscillatorNote, op.levelScale));
    }
    const auto velocityIndex = static_cast<std::size_t>(clampInt(op.velocity, 0, 7));
    const double ampMod = op.ampModEnable ? operatorAmpModFactor(ampDepth, lfoAm) : 1.0;
    double carrierAmp = 0.0;
    double modulatorIndex = 0.0;
    if (!usesChipOperatorPath(renderModel))
    {
        const double level = nextOperatorLevel(opIndex, op.level);
        const double scaledLevel = clampDouble(level - scaleCache.tlOffset * 99.0 / kOppTlMax, 0.0, 99.0);
        const double envelopeDb = amplitudeFactorToDbOffset(envelopeAmp);
        const double ampModDb = op.ampModEnable ? amplitudeFactorToDbOffset(ampMod) : 0.0;
        const double carrierVelocityDbOffset = carrierVelocityDb[velocityIndex];
        const double modulatorVelocityDbOffset = carrier ? carrierVelocityDbOffset : modulatorVelocityDb[velocityIndex];
        const double baseCarrierAmp = usesSharedTypeABase(renderModel)
            ? outputLevelToCarrierAmplitudeChipLike(scaledLevel)
            : outputLevelToCarrierAmplitude(scaledLevel);
        carrierAmp = dbToAmplitude(amplitudeToDb(baseCarrierAmp)
                                   + envelopeDb + carrierVelocityDbOffset + ampModDb);
        const auto& trim = trimForModel(renderModel);
        const double modulatorLevel = usesSharedTypeABase(renderModel)
            ? clampDouble(scaledLevel + trim.modulatorLevelBoost, 0.0, 99.0)
            : scaledLevel;
        const double compatibleModulatorIndex = outputLevelToModulatorIndex(modulatorLevel);
        const double baseModulatorIndex = usesSharedTypeABase(renderModel)
            ? outputLevelToModulatorIndexChipLike(modulatorLevel) * (1.0 - trim.modulatorOpalineBlend)
                + compatibleModulatorIndex * trim.modulatorOpalineBlend
            : compatibleModulatorIndex;
        modulatorIndex = dbToAmplitude(amplitudeToDb(baseModulatorIndex)
                                       + envelopeDb + modulatorVelocityDbOffset + ampModDb)
            * modulatorAttackSofteningForModel(ageSeconds, renderModel);
    }

    double carrierOutput = 0.0;
    double modulatorOutput = 0.0;
    if (usesChipOperatorPath(renderModel))
    {
        const int phaseIndex = chipOperatorPhaseIndex(phases[opSize], phaseModulation, feedback);
        const int scaledTargetLevel = clampInt(op.level - static_cast<int>(scaleCache.tlOffset), 0, 99);
        const double chipTl = nextOperatorTl(opIndex, scaledTargetLevel);
        const double velocityDb = carrier ? carrierVelocityDb[velocityIndex] : modulatorVelocityDb[velocityIndex];
        const double ampModDb = op.ampModEnable ? amplitudeFactorToDbOffset(ampMod) : 0.0;
        const double attenuationIndex = clampDouble(chipEnvelopeIndex + chipTl * kOppTlSubsteps
            + (velocityDb + ampModDb) / kOpmLogAttenuationDbPerStep, 0.0, kOpmEgIndexMax);
        const double opOut = chipOperatorOutputFromAttenuationIndex(phaseIndex, attenuationIndex);
        carrierOutput = opOut;
        modulatorOutput = opOut;
    }
    else
    {
        const double operatorPhase = phases[opSize] + opmPhaseBusToRadians(phaseModulation, renderModel) + feedback;
        const double wave = sineLookup(operatorPhase);
        carrierOutput = wave * carrierAmp;
        modulatorOutput = wave * modulatorIndex;
    }
    const OperatorRender value {
        carrierOutput,
        quantizeOperatorBusForModel(modulatorOutput * kOpmOperatorBusPeak, renderModel)
    };
    if (opIndex == 3)
    {
        feedbackHistory[1] = feedbackHistory[0];
        feedbackHistory[0] = value.modulation;
    }

    computed[opSize] = true;
    outputs[opSize] = value;
    return value;
}

double OpalineVoice::render(const OpalinePatch& patch,
                         const double pitchBend,
                         const int pitchBendRange,
                         const double modWheel,
                         const int modWheelPitchRange,
                         const int modWheelAmpRange,
                         const double globalLfoAge,
                         const OpalineRenderModel renderModel)
{
    activeRenderModel = renderModel;
    ageSeconds += 1.0 / currentSampleRate;

    const auto& algorithm = opalineAlgorithms()[static_cast<std::size_t>(patch.algorithm - 1)];
    if (cachedAlgorithm != patch.algorithm)
    {
        cachedAlgorithm = patch.algorithm;
        operatorCarrierRoles.fill(false);
        for (int index = 0; index < algorithm.carrierCount; ++index)
            operatorCarrierRoles[static_cast<std::size_t>(algorithm.carriers[static_cast<std::size_t>(index)])] = true;
    }
    if (cachedFeedback != patch.feedback)
    {
        cachedFeedback = patch.feedback;
        const int feedback = clampInt(patch.feedback, 0, 7);
        cachedFeedbackDivisor = feedback > 0
            ? std::pow(2.0, static_cast<double>(9 - feedback))
            : 1.0;
    }
    const double lfoAge = patch.lfo.sync ? ageSeconds : globalLfoAge;
    if (cachedLfoSpeed != patch.lfo.speed)
    {
        cachedLfoSpeed = patch.lfo.speed;
        cachedLfoFrequency = opalineLfoSpeedToHz(patch.lfo.speed);
    }
    const double lfoPhase = cachedLfoFrequency * lfoAge;
    const auto lfo = patch.lfo.wave == 3 ? nextSampleAndHoldLfoShape(lfoPhase)
                                         : opalineLfoShape(lfoPhase, patch.lfo.wave);
    const auto pitchLfoShape = clampInt(patch.lfo.pitchSensitivity, 0, 7) == 0
        ? opalineLfoShape(lfoPhase, 2)
        : lfo;
    if (cachedLfoDelay != patch.lfo.delay)
    {
        cachedLfoDelay = patch.lfo.delay;
        const auto timing = lfoDelayTimingForValue(patch.lfo.delay);
        cachedLfoWaitSeconds = timing.waitSeconds;
        cachedLfoFadeSeconds = timing.fadeSeconds;
    }
    const double delay = lfoDelayFactor(patch.lfo.delay, ageSeconds,
                                        cachedLfoWaitSeconds, cachedLfoFadeSeconds);
    const double directPitchLfo = pitchModDepthForModel(patch.lfo.pitchDepth, patch.lfo.pitchSensitivity, patch.lfo.wave, renderModel)
        * delay * pitchLfoShape.second;
    const double safeModWheel = clampDouble(modWheel, 0.0, 1.0);
    const double modWheelPitchLfo = modWheelPitchDepthForModel(modWheelPitchRange, patch.lfo.pitchSensitivity, patch.lfo.wave, renderModel)
        * safeModWheel * pitchLfoShape.second;
    const double pitchLfo = directPitchLfo + modWheelPitchLfo;
    const int combinedAmpDepth = clampInt(patch.lfo.ampDepth
        + static_cast<int>(std::round(static_cast<double>(clampInt(modWheelAmpRange, 0, 99)) * safeModWheel)), 0, 99);
    if (cachedAmpDepthValue != combinedAmpDepth || cachedAmpSensitivity != patch.lfo.ampSensitivity)
    {
        cachedAmpDepthValue = combinedAmpDepth;
        cachedAmpSensitivity = patch.lfo.ampSensitivity;
        cachedAmpDepth = opmStyleAmpModDepth(combinedAmpDepth, patch.lfo.ampSensitivity);
    }
    const double ampDepth = cachedAmpDepth;
    const double appliedPitchLfo = nextPitchModulation(pitchLfo);
    const double pitchEnvelopeSemitones = pitchEnvelope.nextSemitones();
    if (portamentoOffsetSemitones > 0.0)
        portamentoOffsetSemitones = std::max(0.0, portamentoOffsetSemitones - portamentoStepPerSample);
    else if (portamentoOffsetSemitones < 0.0)
        portamentoOffsetSemitones = std::min(0.0, portamentoOffsetSemitones + portamentoStepPerSample);

    const double bendSemitones = clampDouble(pitchBend, -1.0, 1.0)
        * static_cast<double>(clampInt(pitchBendRange, 0, 12));
    const int baseMidiNote = midiNote + patch.transpose;
    if (cachedBaseMidiNote != baseMidiNote)
    {
        cachedBaseMidiNote = baseMidiNote;
        cachedBaseNoteFrequency = midiNoteToFrequency(static_cast<double>(baseMidiNote));
    }
    const double baseFrequency = cachedBaseNoteFrequency
        * std::pow(2.0, (portamentoOffsetSemitones + bendSemitones
                         + appliedPitchLfo + pitchEnvelopeSemitones) / 12.0);

    std::array<bool, kOperatorCount> computed {};
    std::array<OperatorRender, kOperatorCount> outputs {};
    double sum = 0.0;

    for (int i = 0; i < algorithm.carrierCount; ++i)
    {
        const int carrier = algorithm.carriers[static_cast<std::size_t>(i)];
        sum += renderOperator(carrier,
                              patch,
                              algorithm,
                              baseFrequency,
                              ampDepth,
                              lfo.first,
                              renderModel,
                              computed,
                              outputs)
                   .audio;
    }

    if (!std::isfinite(sum))
    {
        failed = true;
        feedbackHistory.fill(0.0);
        return 0.0;
    }

    if (algorithm.carrierCount <= 0)
        return 0.0;

    if (cachedCarrierCount != algorithm.carrierCount)
    {
        cachedCarrierCount = algorithm.carrierCount;
        cachedChipCarrierGain = std::pow(static_cast<double>(algorithm.carrierCount),
                                         -kChipCarrierCountCompensationExponent);
        cachedCarrierDivisor = std::sqrt(static_cast<double>(algorithm.carrierCount));
    }

    double mixed = 0.0;
    if (usesChipOperatorPath(renderModel))
    {
        mixed = chipMixerOutputFromBus(sum * kOpmOperatorBusPeak) * cachedChipCarrierGain;
    }
    else
    {
        mixed = (sum / cachedCarrierDivisor) * kCarrierMixGain;
        if (usesSharedTypeABase(renderModel))
            mixed *= trimForModel(renderModel).outputGain;
    }
    return quantizeVoiceOutputForModel(mixed, renderModel);
}
} // namespace opaline
