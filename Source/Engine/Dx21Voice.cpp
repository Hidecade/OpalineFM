#include "Engine/Dx21Voice.h"

#include "Engine/Dx21Tables.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace dx21
{
namespace
{
// LEVEL、TL、変調指数の調整係数。ChipHybridでは従来値とOPM風の値を混ぜる。
constexpr double kCarrierLevelDbRange = 48.0;
constexpr double kModulatorIndexScale = 1.08;
constexpr double kModulatorIndexBlend = 0.08;
constexpr double kModulatorIndexExponent = 2.2;
constexpr double kOpmTlDbPerStep = 0.75;
constexpr double kOpmLogAttenuationDbPerStep = 6.020599913279624 / 256.0;
constexpr double kOpmEgDbRange = 128.0;
constexpr double kOpmEgIndexMax = 1023.0;
constexpr double kChipLevelBlend = 0.50;
constexpr double kChipModulatorBlend = 0.25;
constexpr double kChipModulatorIndexScale = 1.08;
constexpr double kModulatorAttackSoftenSeconds = 0.003;
constexpr double kChipPhaseModGain = 1.6;
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
constexpr std::array<double, 8> kDx21PitchSensitivitySemitones {
    2.0,    // PMS=0 uses the DX21 vibrato oscillator path; measured like PMS=5.
    0.125,
    0.25,
    0.5,
    1.0,
    2.0,
    4.0,
    7.0
};
constexpr std::array<double, 8> kOpmPitchSensitivitySemitones {
    1.0,    // PMS=0 uses the DX21 VIBRATO OSC path with PMS=5 depth.
    0.05,
    0.10,
    0.20,
    0.50,
    1.00,
    4.00,
    7.00
};

int nextNukedOpmNoiseInjectedBit(std::uint32_t& noiseLfsr, int& noiseBit)
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

int nextNukedOpmSampleAndHoldValue(std::uint32_t& noiseLfsr, int& noiseBit)
{
    int value = 0;
    for (int bit = 0; bit < 8; ++bit)
        value = (value << 1) | nextNukedOpmNoiseInjectedBit(noiseLfsr, noiseBit);

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

double dx21LevelToOpmTl(const double level)
{
    // DX21のLEVEL 0..99を、OPMのTL 0..127へ写像する。TLは小さいほど音量が大きい。
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
    return opmTlToAmplitude(dx21LevelToOpmTl(level));
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

double keyboardScaledLevel(const double level, const int levelScale, const int note)
{
    const double scale = clampDouble(static_cast<double>(levelScale), 0.0, 99.0);
    if (scale <= 0.0)
        return level;

    const double highKeyAmount = clampDouble((static_cast<double>(note) - 60.0) / 36.0, 0.0, 1.0);
    return clampDouble(level - highKeyAmount * scale * 0.45, 0.0, 99.0);
}

double keyboardScaleTlOffset(const int note, const int levelScale)
{
    const double scale = clampDouble(static_cast<double>(levelScale), 0.0, 99.0);
    if (scale <= 0.0)
        return 0.0;

    const double highKeyAmount = clampDouble((static_cast<double>(note) - 60.0) / 36.0, 0.0, 1.0);
    return highKeyAmount * scale * 0.45 * kOppTlMax / 99.0;
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

double modulatorAttackSofteningForModel(const double age, const Dx21RenderModel renderModel)
{
    if (renderModel == Dx21RenderModel::ChipHybrid)
        return 1.0;

    return modulatorAttackSoftening(age);
}

double envelopeAmpForModel(const double envelopeAmp, const Dx21RenderModel renderModel)
{
    if (renderModel != Dx21RenderModel::ChipHybrid)
        return envelopeAmp;

    const double db = amplitudeFactorToDbOffset(envelopeAmp);
    const double egIndex = std::round(clampDouble(db, 0.0, kOpmEgDbRange) * kOpmEgIndexMax / kOpmEgDbRange);
    const double quantizedDb = clampDouble(egIndex, 0.0, kOpmEgIndexMax) * kOpmEgDbRange / kOpmEgIndexMax;
    return dbToAmplitude(quantizedDb);
}

bool isCarrierOperator(const Algorithm& algorithm, const int opIndex)
{
    for (int i = 0; i < algorithm.carrierCount; ++i)
    {
        if (algorithm.carriers[static_cast<std::size_t>(i)] == opIndex)
            return true;
    }

    return false;
}

double operatorVelocityFactor(const int opVelocity, const int noteVelocity, const bool carrier)
{
    const double amount = clampDouble(static_cast<double>(opVelocity), 0.0, 7.0) / 7.0;
    if (amount <= 0.0)
        return 1.0;

    const double velocity = clampDouble(static_cast<double>(noteVelocity), 1.0, 127.0) / 127.0;
    const double shapedVelocity = std::pow(velocity, 1.35);
    const double minimum = carrier ? 0.20 : 0.32;
    const double maximum = carrier ? 1.85 : 2.55;
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

double opmPhaseBusToRadians(const double bus, const Dx21RenderModel renderModel)
{
    const double gain = renderModel == Dx21RenderModel::ChipHybrid ? kChipPhaseModGain : 1.0;
    return opmPhaseBusToRadians(bus) * gain;
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
    // NEWでは1024stepのテーブルindex上で、PMとFBを整数加算する。
    const int baseIndex = chipOperatorPhaseIndex(phase);
    const int modulationIndex = chipPhaseOffsetIndexFromRadians(opmPhaseBusToRadians(modulationBus, Dx21RenderModel::ChipHybrid));
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

double quantizeOperatorAudioForModel(const double audio, const Dx21RenderModel renderModel)
{
    if (renderModel != Dx21RenderModel::ChipHybrid)
        return audio;

    return clampDouble(std::round(audio * 8191.0) / 8192.0, -1.0, 1.0);
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

double chipOperatorOutputFromPhaseIndex(const int phaseIndex, const double amplitude)
{
    if (amplitude <= 0.0)
        return 0.0;

    const int waveAttenuation = chipOperatorLogSineAttenuation(phaseIndex);
    const int ampAttenuation = clampInt(
        static_cast<int>(std::round(amplitudeFactorToDbOffset(amplitude) / kOpmLogAttenuationDbPerStep)), 0, 4095);
    const int attenuation = clampInt(waveAttenuation + ampAttenuation, 0, 4095);
    const double output = static_cast<double>(chipOperatorSign(phaseIndex)) * chipExpOutputFromAttenuation(attenuation);
    return quantizeOperatorAudioForModel(output, Dx21RenderModel::ChipHybrid);
}

double quantizeOperatorBusForModel(const double bus, const Dx21RenderModel renderModel)
{
    if (renderModel != Dx21RenderModel::ChipHybrid)
        return bus;

    return clampDouble(std::round(bus), -kOpmOperatorBusPeak, kOpmOperatorBusPeak - 1.0);
}

double mixPhaseModulationForModel(const double sum, const int inputCount, const Dx21RenderModel renderModel)
{
    if (renderModel != Dx21RenderModel::ChipHybrid || inputCount <= 1)
        return sum;

    const double mixed = sum / static_cast<double>(std::min(inputCount, 2));
    return quantizeOperatorBusForModel(mixed, renderModel);
}

double quantizeVoiceOutputForModel(const double sample, const Dx21RenderModel renderModel)
{
    if (renderModel != Dx21RenderModel::ChipHybrid)
        return sample;

    return clampDouble(std::round(sample * 8191.0) / 8192.0, -1.0, 1.0);
}

double feedbackHistoryBusForModel(const std::array<double, 2>& history, const Dx21RenderModel renderModel)
{
    const double sum = history[0] + history[1];
    if (renderModel != Dx21RenderModel::ChipHybrid)
        return sum;

    return quantizeOperatorBusForModel(sum * 0.5, renderModel);
}

double opmFeedbackBusToRadians(const double bus, const int level, const Dx21RenderModel renderModel)
{
    const int feedback = clampInt(level, 0, 7);
    if (feedback <= 0)
        return 0.0;

    if (renderModel == Dx21RenderModel::ChipHybrid)
    {
        static constexpr std::array<double, 8> kFeedbackGainTable {
            0.0,
            1.0 / 256.0,
            1.0 / 128.0,
            1.0 / 64.0,
            1.0 / 32.0,
            1.0 / 16.0,
            1.0 / 8.0,
            1.0 / 4.0
        };
        return opmPhaseBusToRadians(bus, renderModel) * kFeedbackGainTable[static_cast<std::size_t>(feedback)];
    }

    const int shift = feedback + 6;
    return bus * std::pow(2.0, static_cast<double>(shift)) * 2.0 * kPi
        / (kOpmSineIndexSteps * 65536.0) * kOpmBusPhaseGain;
}

double levelToOppTlTarget(const double level)
{
    return std::round((1.0 - clampDouble(level, 0.0, 99.0) / 99.0) * kOppTlMax);
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
    return normalized * kDx21PitchSensitivitySemitones[sensitivityIndex];
}

double chipStylePitchModDepth(const int depth, const int sensitivity)
{
    const double normalized = static_cast<double>(clampInt(depth, 0, 99)) / 99.0;
    const auto sensitivityIndex = static_cast<std::size_t>(clampInt(sensitivity, 0, 7));
    return normalized * kOpmPitchSensitivitySemitones[sensitivityIndex];
}

double pitchModDepthForModel(const int depth, const int sensitivity, const int wave, const Dx21RenderModel renderModel)
{
    if (clampInt(wave, 0, 3) == 1 && clampInt(sensitivity, 0, 7) == 7)
        return static_cast<double>(clampInt(depth, 0, 99)) / 99.0 * 8.0;

    if (renderModel == Dx21RenderModel::ChipHybrid || renderModel == Dx21RenderModel::Current)
        return chipStylePitchModDepth(depth, sensitivity);

    return opmStylePitchModDepth(depth, sensitivity);
}

double modWheelPitchDepthForModel(const int sensitivity, const int wave, const Dx21RenderModel renderModel)
{
    return pitchModDepthForModel(99, sensitivity, wave, renderModel);
}

double opmStyleAmpModDepth(const int depth, const int sensitivity)
{
    static constexpr std::array<double, 9> kDepthTable { 0.0, 0.025, 0.055, 0.11, 0.22, 0.38, 0.58, 0.78, 0.96 };
    const double normalized = static_cast<double>(clampInt(depth, 0, 99)) / 99.0;
    const double normalizedSensitivity = static_cast<double>(clampInt(sensitivity, 0, 3)) / 3.0;
    return normalizedSensitivity * steppedModDepth(normalized, kDepthTable);
}

double lfoDelayFactor(const int delay, const double age)
{
    if (delay <= 0)
        return 1.0;

    const double waitSeconds = 0.25 * std::pow(2.0, static_cast<double>(clampInt(delay, 0, 99)) / 25.0);
    const double highDelay = clampDouble((static_cast<double>(delay) - 75.0) / 24.0, 0.0, 1.0);
    const double fadeSeconds = waitSeconds * (1.0 + 2.0 * highDelay);
    if (age <= waitSeconds)
        return 0.0;

    return std::min(1.0, (age - waitSeconds) / std::max(0.001, fadeSeconds));
}

std::pair<double, double> dx21LfoShape(const double phase, const int wave)
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

std::pair<double, double> dx21PitchLfoShape(const double phase, const int wave, const int sensitivity)
{
    if (clampInt(sensitivity, 0, 7) != 0)
        return dx21LfoShape(phase, wave);

    return dx21LfoShape(phase, 2);
}
}

void Dx21Voice::start(const Dx21Patch& patch, const int newMidiNote, const int velocity, const double sampleRate)
{
    midiNote = newMidiNote;
    noteVelocity = clampInt(velocity, 0, 127);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    ageSeconds = 0.0;
    phases.fill(0.0);
    operatorTlAccumulators.fill(0.0);
    delayedPitchLfo = 0.0;
    sampleAndHoldLfsr = 0;
    sampleAndHoldBit = 0;
    sampleAndHoldCycle = -1;
    sampleAndHoldValue = 128;
    feedbackHistory.fill(0.0);
    failed = false;
    activeRenderModel = Dx21RenderModel::Current;
    pitchEnvelope.reset(currentSampleRate);
    pitchEnvelope.noteOn(patch.pitchEnvelope);

    for (int i = 0; i < kOperatorCount; ++i)
    {
        operatorOppTlUnits[static_cast<std::size_t>(i)] =
            levelToOppTlTarget(patch.operators[static_cast<std::size_t>(i)].level) * kOppTlSubsteps;
        envelopes[static_cast<std::size_t>(i)].reset(currentSampleRate);
        envelopes[static_cast<std::size_t>(i)].noteOn(patch.operators[static_cast<std::size_t>(i)].envelope,
                                                      patch.operators[static_cast<std::size_t>(i)].rateScale,
                                                      midiNote);
        chipEnvelopes[static_cast<std::size_t>(i)].reset(currentSampleRate);
        chipEnvelopes[static_cast<std::size_t>(i)].noteOn(patch.operators[static_cast<std::size_t>(i)].envelope,
                                                          patch.operators[static_cast<std::size_t>(i)].rateScale,
                                                          midiNote);
    }
}

void Dx21Voice::release()
{
    for (auto& envelope : envelopes)
        envelope.noteOff();
    for (auto& envelope : chipEnvelopes)
        envelope.noteOff();
    pitchEnvelope.noteOff();
}

bool Dx21Voice::isActive() const
{
    if (failed)
        return false;

    if (activeRenderModel == Dx21RenderModel::ChipHybrid)
    {
        for (const auto& envelope : chipEnvelopes)
        {
            if (envelope.isActive())
                return true;
        }
    }
    else
    {
        for (const auto& envelope : envelopes)
        {
            if (envelope.isActive())
                return true;
        }
    }
    return false;
}

double Dx21Voice::nextOperatorLevel(const int index, const int targetLevel)
{
    return oppTlUnitsToLevel(nextOperatorTl(index, targetLevel) * kOppTlSubsteps);
}

double Dx21Voice::nextOperatorTl(const int index, const int targetLevel)
{
    const auto opSize = static_cast<std::size_t>(index);
    double current = operatorOppTlUnits[opSize];
    const double target = levelToOppTlTarget(targetLevel);
    const double stepInterval = std::max(1.0, currentSampleRate * kOppTlRampSeconds / (kOppTlMax * kOppTlSubsteps));

    operatorTlAccumulators[opSize] += 1.0;
    const bool match = operatorTlAccumulators[opSize] >= stepInterval;
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
        operatorTlAccumulators[opSize] -= stepInterval;

    operatorOppTlUnits[opSize] = clampDouble(current, 0.0, kOppTlMax * kOppTlSubsteps);
    return operatorOppTlUnits[opSize] / kOppTlSubsteps;
}

double Dx21Voice::nextPitchModulation(const double pitchLfo)
{
    const double delayed = delayedPitchLfo;
    delayedPitchLfo = pitchLfo;
    return delayed;
}

std::pair<double, double> Dx21Voice::nextSampleAndHoldLfoShape(const double phase)
{
    const int cycle = static_cast<int>(std::floor(phase));
    if (cycle != sampleAndHoldCycle)
    {
        sampleAndHoldValue = nextNukedOpmSampleAndHoldValue(sampleAndHoldLfsr, sampleAndHoldBit);
        sampleAndHoldCycle = cycle;
    }

    const double am = static_cast<double>(sampleAndHoldValue) / 255.0;
    const double pm = static_cast<double>(sampleAndHoldValue) / 127.5 - 1.0;
    return { clampDouble(am, 0.0, 1.0), clampDouble(pm, -1.0, 1.0) };
}

OperatorRender Dx21Voice::renderOperator(const int opIndex,
                                         const Dx21Patch& patch,
                                         const Algorithm& algorithm,
                                         const double baseFrequency,
                                         const double ampDepth,
                                         const double lfoAm,
                                         const Dx21RenderModel renderModel,
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
    phaseModulation = mixPhaseModulationForModel(phaseModulation, algorithm.depCounts[opSize], renderModel);

    const double feedback = opIndex == 3
        ? opmFeedbackBusToRadians(feedbackHistoryBusForModel(feedbackHistory, renderModel), patch.feedback, renderModel)
        : 0.0;

    const double ratio = dx21Ratios()[static_cast<std::size_t>(op.ratioIndex)];
    const double frequency = baseFrequency * ratio + opmStyleDt1FrequencyOffset(baseFrequency, ratio, op.detune, midiNote);
    phases[opSize] += opmStylePhaseAdvance(frequency, currentSampleRate);
    if (phases[opSize] > 2.0 * kPi)
        phases[opSize] -= 2.0 * kPi;

    const double rawEnvelopeAmp = renderModel == Dx21RenderModel::ChipHybrid
        ? chipEnvelopes[opSize].next()
        : envelopes[opSize].next();
    const double envelopeAmp = envelopeAmpForModel(rawEnvelopeAmp, renderModel);
    const bool carrier = isCarrierOperator(algorithm, opIndex);
    const double carrierVelocityFactor = operatorVelocityFactor(op.velocity, noteVelocity, true);
    const double modulatorVelocityFactor = operatorVelocityFactor(op.velocity, noteVelocity, false);
    const double ampMod = op.ampModEnable ? operatorAmpModFactor(ampDepth, lfoAm) : 1.0;
    double carrierAmp = 0.0;
    double modulatorIndex = 0.0;

    if (renderModel == Dx21RenderModel::ChipHybrid)
    {
        // NEWはTL/dB/EG/Velocity/AMをログ領域でまとめ、従来モデルとブレンドする。
        const double tl = nextOperatorTl(opIndex, op.level);
        const double smoothedLevel = oppTlUnitsToLevel(tl * kOppTlSubsteps);
        const double oldScaledLevel = keyboardScaledLevel(smoothedLevel, op.levelScale, midiNote);
        const double chipTl = clampDouble(tl + keyboardScaleTlOffset(midiNote, op.levelScale), 0.0, kOppTlMax);
        const double envelopeDb = amplitudeFactorToDbOffset(envelopeAmp);
        const double ampModDb = op.ampModEnable ? amplitudeFactorToDbOffset(ampMod) : 0.0;

        const double oldCarrierLevelDb = amplitudeToDb(outputLevelToCarrierAmplitude(oldScaledLevel));
        const double chipCarrierLevelDb = opmTlToDb(chipTl);
        const double carrierLevelDb = oldCarrierLevelDb * (1.0 - kChipLevelBlend) + chipCarrierLevelDb * kChipLevelBlend;
        const double carrierVelocityDb = amplitudeFactorToDbOffset(carrierVelocityFactor);
        carrierAmp = dbToAmplitude(carrierLevelDb + envelopeDb + carrierVelocityDb + ampModDb);

        const double oldModulatorIndex = outputLevelToModulatorIndex(oldScaledLevel);
        const double chipModulatorDb = opmTlToDb(chipTl) + envelopeDb
            + amplitudeFactorToDbOffset(carrier ? carrierVelocityFactor : modulatorVelocityFactor) + ampModDb;
        const double chipModulatorIndex = dbToAmplitude(chipModulatorDb) * kChipModulatorIndexScale;
        modulatorIndex = oldModulatorIndex * envelopeAmp
                * (carrier ? carrierVelocityFactor : modulatorVelocityFactor) * ampMod
                * (1.0 - kChipModulatorBlend)
            + chipModulatorIndex * kChipModulatorBlend;
        modulatorIndex *= modulatorAttackSofteningForModel(ageSeconds, renderModel);
    }
    else
    {
        const double level = nextOperatorLevel(opIndex, op.level);
        const double scaledLevel = keyboardScaledLevel(level, op.levelScale, midiNote);
        const double envelopeDb = amplitudeFactorToDbOffset(envelopeAmp);
        const double ampModDb = op.ampModEnable ? amplitudeFactorToDbOffset(ampMod) : 0.0;
        const double carrierVelocityDb = amplitudeFactorToDbOffset(carrierVelocityFactor);
        const double modulatorVelocityDb = amplitudeFactorToDbOffset(carrier ? carrierVelocityFactor : modulatorVelocityFactor);
        carrierAmp = dbToAmplitude(amplitudeToDb(outputLevelToCarrierAmplitude(scaledLevel))
                                   + envelopeDb + carrierVelocityDb + ampModDb);
        modulatorIndex = dbToAmplitude(amplitudeToDb(outputLevelToModulatorIndex(scaledLevel))
                                       + envelopeDb + modulatorVelocityDb + ampModDb)
            * modulatorAttackSofteningForModel(ageSeconds, renderModel);
    }

    double carrierOutput = 0.0;
    double modulatorOutput = 0.0;
    if (renderModel == Dx21RenderModel::ChipHybrid)
    {
        const int phaseIndex = chipOperatorPhaseIndex(phases[opSize], phaseModulation, feedback);
        carrierOutput = chipOperatorOutputFromPhaseIndex(phaseIndex, carrierAmp);
        modulatorOutput = chipOperatorOutputFromPhaseIndex(phaseIndex, modulatorIndex);
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

double Dx21Voice::render(const Dx21Patch& patch,
                         const double pitchBend,
                         const double modWheel,
                         const double globalLfoAge,
                         const Dx21RenderModel renderModel)
{
    activeRenderModel = renderModel;
    ageSeconds += 1.0 / currentSampleRate;

    const auto& algorithm = dx21Algorithms()[static_cast<std::size_t>(patch.algorithm - 1)];
    const double lfoAge = patch.lfo.sync ? ageSeconds : globalLfoAge;
    const double lfoPhase = dx21LfoSpeedToHz(patch.lfo.speed) * lfoAge;
    const auto lfo = patch.lfo.wave == 3 ? nextSampleAndHoldLfoShape(lfoPhase)
                                         : dx21LfoShape(lfoPhase, patch.lfo.wave);
    const auto pitchLfoShape = clampInt(patch.lfo.pitchSensitivity, 0, 7) == 0
        ? dx21LfoShape(lfoPhase, 2)
        : lfo;
    const double delay = lfoDelayFactor(patch.lfo.delay, ageSeconds);
    const double directPitchLfo = pitchModDepthForModel(patch.lfo.pitchDepth, patch.lfo.pitchSensitivity, patch.lfo.wave, renderModel)
        * delay * pitchLfoShape.second;
    const double modWheelPitchLfo = modWheelPitchDepthForModel(patch.lfo.pitchSensitivity, patch.lfo.wave, renderModel)
        * clampDouble(modWheel, 0.0, 1.0) * pitchLfoShape.second;
    const double pitchLfo = directPitchLfo + modWheelPitchLfo;
    const double ampDepth = opmStyleAmpModDepth(patch.lfo.ampDepth, patch.lfo.ampSensitivity) * delay;
    const double appliedPitchLfo = nextPitchModulation(pitchLfo);
    const double pitchEnvelopeSemitones = pitchEnvelope.nextSemitones();
    const double bendSemitones = clampDouble(pitchBend, -1.0, 1.0) * 2.0;
    const double baseFrequency = midiNoteToFrequency(static_cast<double>(midiNote + patch.transpose))
        * std::pow(2.0, (bendSemitones + appliedPitchLfo + pitchEnvelopeSemitones) / 12.0);

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

    const double mixed = (sum / std::sqrt(static_cast<double>(algorithm.carrierCount))) * kCarrierMixGain;
    return quantizeVoiceOutputForModel(mixed, renderModel);
}
} // namespace dx21
