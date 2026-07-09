#include "Engine/OpalineEngine.h"
#include "Engine/OpalineEnvelope.h"
#include "Engine/OpalinePitchEnvelope.h"
#include "Engine/OpalineSysex.h"
#include "Engine/OpalineTables.h"
#include "Engine/OpalineVoiceLibrary.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void expectNear(double actual, double expected, double tolerance, const std::string& message)
{
    if (std::abs(actual - expected) > tolerance)
    {
        ++failures;
        std::cerr << "FAIL: " << message << " actual=" << actual << " expected=" << expected << '\n';
    }
}

void testTables()
{
    const auto& ratios = opaline::opalineRatios();
    expect(ratios.size() == 64, "compatible ratio table has 64 entries");
    expectNear(ratios[0], 0.50, 0.0001, "ratio[0]");
    expectNear(ratios[4], 1.00, 0.0001, "ratio[4]");
    expectNear(ratios[63], 25.95, 0.0001, "ratio[63]");

    const auto& algorithms = opaline::opalineAlgorithms();
    expect(algorithms.size() == 8, "algorithm table has 8 entries");
    expect(algorithms[0].carrierCount == 1 && algorithms[0].carriers[0] == 0, "algorithm 1 carrier");
    expect(algorithms[0].depCounts[0] == 1 && algorithms[0].deps[0][0] == 1, "algorithm 1 OP2 modulates OP1");
    expect(algorithms[2].carrierCount == 1 && algorithms[2].depCounts[0] == 2
               && algorithms[2].deps[0][0] == 1 && algorithms[2].deps[0][1] == 3,
           "algorithm 3 matches Webcompatible dependency graph");
    expect(algorithms[3].carrierCount == 1 && algorithms[3].depCounts[0] == 2
               && algorithms[3].deps[0][0] == 1 && algorithms[3].deps[0][1] == 2,
           "algorithm 4 matches Webcompatible dependency graph");
    expect(algorithms[7].carrierCount == 4, "algorithm 8 has four carriers");
    expectNear(opaline::opalineLfoSpeedToHz(35), 6.7, 0.02, "LFO speed 35 matches compatible manual");
    expectNear(opaline::opalineLfoSpeedToHz(99), 55.0, 0.0001, "LFO speed 99 matches Webcompatible");
}

void testPatchNormalization()
{
    opaline::OpalinePatch patch;
    patch.algorithm = 99;
    patch.feedback = -5;
    patch.transpose = 100;
    patch.lfo.wave = 42;
    patch.pitchEnvelope.rate1 = -4;
    patch.pitchEnvelope.rate2 = 100;
    patch.pitchEnvelope.rate3 = 123;
    patch.pitchEnvelope.level1 = -5;
    patch.pitchEnvelope.level2 = 101;
    patch.pitchEnvelope.level3 = 120;
    patch.effects.reverb = 1000;
    patch.effects.mix = -10;
    patch.effects.echoMix = 300;
    patch.effects.tone = 120;
    patch.effects.chorus = 200;
    patch.effects.delay = 300;
    patch.operators[0].ratioIndex = 999;
    patch.operators[0].detune = -99;
    patch.operators[0].envelope.decay1Level = 100;

    const auto normalized = opaline::normalizePatch(patch);
    expect(normalized.algorithm == 8, "algorithm clamps high");
    expect(normalized.feedback == 0, "feedback clamps low");
    expect(normalized.transpose == 24, "transpose clamps high");
    expect(normalized.lfo.wave == 3, "lfo wave clamps high");
    expect(normalized.pitchEnvelope.rate1 == 0, "PEG rate1 clamps low");
    expect(normalized.pitchEnvelope.rate2 == 99, "PEG rate2 clamps high");
    expect(normalized.pitchEnvelope.rate3 == 99, "PEG rate3 clamps high");
    expect(normalized.pitchEnvelope.level1 == 0, "PEG level1 clamps low");
    expect(normalized.pitchEnvelope.level2 == 99, "PEG level2 clamps high");
    expect(normalized.pitchEnvelope.level3 == 99, "PEG level3 clamps high");
    expect(normalized.effects.reverb == 99, "effect reverb clamps high");
    expect(normalized.effects.mix == 0, "effect mix clamps low");
    expect(normalized.effects.echoMix == 99, "effect echo mix clamps high");
    expect(normalized.effects.tone == 99, "effect tone clamps high");
    expect(normalized.effects.chorus == 99, "effect chorus clamps high");
    expect(normalized.effects.delay == 99, "effect delay clamps high");
    expect(normalized.operators[0].ratioIndex == 63, "ratio index clamps high");
    expect(normalized.operators[0].detune == -3, "detune clamps low");
    expect(normalized.operators[0].envelope.decay1Level == 15, "D1L clamps high");
}

void testEnvelope()
{
    opaline::OpalineEnvelope envelope;
    envelope.reset(44100.0);

    opaline::OpalineEnvelopeParams params;
    params.attackRate = 31;
    params.decay1Rate = 20;
    params.decay1Level = 10;
    params.decay2Rate = 8;
    params.releaseRate = 12;

    envelope.noteOn(params, 0, 60);
    bool reachedNonZero = false;
    for (int i = 0; i < 44100; ++i)
        reachedNonZero = reachedNonZero || envelope.next() > 0.001;

    expect(reachedNonZero, "envelope produces audible attack");
    expect(envelope.isActive(), "envelope remains active before noteOff");

    envelope.noteOff();
    for (int i = 0; i < 44100 * 3; ++i)
        envelope.next();

    expect(!envelope.isActive(), "envelope eventually releases");
}

void testFastAttackTiming()
{
    opaline::OpalineEnvelope envelope;
    envelope.reset(44100.0);

    opaline::OpalineEnvelopeParams params;
    params.attackRate = 31;
    params.decay1Rate = 0;
    params.decay1Level = 15;
    params.decay2Rate = 0;
    params.releaseRate = 15;

    envelope.noteOn(params, 0, 60);
    double ampAt100Ms = 0.0;
    for (int i = 0; i < 4410; ++i)
        ampAt100Ms = envelope.next();

    expect(ampAt100Ms > 0.5, "AR31 reaches a strong level within 100 ms");
    expect(envelope.stage() != opaline::OpalineEnvelope::Stage::Attack, "AR31 leaves attack stage quickly");
}

void testD1L15SkipsToDecay2()
{
    auto sampleEnvelope = [](const int decay1Level)
    {
        opaline::OpalineEnvelope envelope;
        envelope.reset(44100.0);

        opaline::OpalineEnvelopeParams params;
        params.attackRate = 31;
        params.decay1Rate = 0;
        params.decay1Level = decay1Level;
        params.decay2Rate = 7;
        params.releaseRate = 3;

        envelope.noteOn(params, 0, 60);
        double value = 0.0;
        for (int i = 0; i < 44100 * 3; ++i)
            value = envelope.next();

        return value;
    };

    const double d1l14 = sampleEnvelope(14);
    const double d1l15 = sampleEnvelope(15);
    expect(d1l14 > 0.5, "D1L14 sustains when D1R is zero");
    expect(d1l15 < d1l14 * 0.5, "D1L15 decays through D2R even when D1R is zero");
}

void testPitchEnvelope()
{
    opaline::OpalinePitchEnvelope envelope;
    envelope.reset(1000.0);

    opaline::OpalinePitchEnvelopeParams params;
    params.rate1 = 90;
    params.rate2 = 99;
    params.rate3 = 99;
    params.level1 = 99;
    params.level2 = 50;
    params.level3 = 50;

    envelope.noteOn(params);
    const double first = envelope.nextSemitones();
    expect(first > 0.0 && first < 48.0, "PEG starts moving from PL3 toward PL1");

    double value = first;
    for (int i = 0; i < 500; ++i)
        value = envelope.nextSemitones();

    expectNear(value, 0.0, 0.0001, "PEG reaches PL2 sustain after stage 1 and 2");
    expect(envelope.stage() == opaline::OpalinePitchEnvelope::Stage::Sustain, "PEG enters sustain");

    params.level1 = 50;
    params.level2 = 99;
    params.level3 = 50;
    params.rate1 = 99;
    params.rate2 = 99;
    params.rate3 = 99;
    envelope.noteOn(params);
    for (int i = 0; i < 50; ++i)
        value = envelope.nextSemitones();

    expectNear(value, 48.0, 0.0001, "PEG sustains PL2");
    envelope.noteOff();
    for (int i = 0; i < 50; ++i)
        value = envelope.nextSemitones();

    expectNear(value, 0.0, 0.0001, "PEG releases from current value to PL3");
    expect(envelope.stage() == opaline::OpalinePitchEnvelope::Stage::Finished, "PEG finishes release");

    envelope.reset(1000.0);
    params.rate1 = 99;
    params.rate2 = 99;
    params.rate3 = 99;
    params.level1 = 99;
    params.level2 = 99;
    params.level3 = 50;
    envelope.noteOn(params);
    for (int i = 0; i < 3; ++i)
        value = envelope.nextSemitones();

    expectNear(value, 0.0, 0.0001, "PEG PR99 holds PL3 before jump");
    value = envelope.nextSemitones();
    expectNear(value, 48.0, 0.0001, "PEG PR99 jumps to PL1 after delay");
    expect(envelope.stage() == opaline::OpalinePitchEnvelope::Stage::Sustain, "PEG PR99 reaches sustain after jump");

    struct PegRateExpectation
    {
        int rate = 0;
        int expectedSamples = 0;
    };

    const PegRateExpectation measuredRates[] {
        { 20, 5583 },
        { 40, 2403 },
        { 60, 1129 },
        { 80, 579 },
        { 90, 270 },
        { 91, 258 },
        { 92, 240 },
        { 93, 230 },
        { 94, 216 },
        { 95, 204 },
        { 96, 179 },
        { 97, 175 },
        { 98, 155 },
        { 99, 4 }
    };

    for (const auto& expected : measuredRates)
    {
        envelope.reset(1000.0);
        params.rate1 = expected.rate;
        params.rate2 = 99;
        params.rate3 = 99;
        params.level1 = 99;
        params.level2 = 99;
        params.level3 = 50;
        envelope.noteOn(params);

        int samples = 0;
        value = 0.0;
        while (envelope.stage() != opaline::OpalinePitchEnvelope::Stage::Sustain && samples < expected.expectedSamples + 200)
        {
            value = envelope.nextSemitones();
            ++samples;
        }

        expectNear(static_cast<double>(samples),
                   static_cast<double>(expected.expectedSamples),
                   6.0,
                   "PEG measured PR reaches PL1/PL2 in expected time");
        expectNear(value, 48.0, 0.0001, "PEG measured PR reaches PL99");
    }

    struct PegLevelExpectation
    {
        int level = 50;
        double expectedSemitones = 0.0;
    };

    const PegLevelExpectation measuredLevels[] {
        { 0, -48.0 },
        { 5, -40.5284 },
        { 10, -33.0135 },
        { 25, -17.0667 },
        { 35, -7.5298 },
        { 40, -4.9894 },
        { 45, -2.5853 },
        { 47, -1.55118 },
        { 50, 0.0 },
        { 64, 6.9846 },
        { 65, 8.0370 },
        { 80, 23.4970 },
        { 90, 34.5158 },
        { 99, 48.0 }
    };

    for (const auto& expected : measuredLevels)
    {
        envelope.reset(1000.0);
        params.rate1 = 99;
        params.rate2 = 99;
        params.rate3 = 99;
        params.level1 = expected.level;
        params.level2 = expected.level;
        params.level3 = 50;
        envelope.noteOn(params);
        value = envelope.nextSemitones();
        for (int i = 1; i < 32 && envelope.stage() != opaline::OpalinePitchEnvelope::Stage::Sustain; ++i)
            value = envelope.nextSemitones();

        expectNear(value,
                   expected.expectedSemitones,
                   0.0001,
                   "PEG measured PL reaches expected pitch offset");
    }
}

void testEngineRendering()
{
    opaline::OpalinePatch patch;
    patch.algorithm = 8;
    patch.feedback = 0;
    for (auto& op : patch.operators)
    {
        op.level = 0;
        op.envelope.attackRate = 31;
        op.envelope.decay1Rate = 12;
        op.envelope.decay1Level = 15;
        op.envelope.decay2Rate = 0;
        op.envelope.releaseRate = 15;
    }
    patch.operators[0].level = 95;
    patch.operators[0].ratioIndex = 4;

    opaline::OpalineEngine engine;
    engine.prepare(44100.0, 4);
    engine.setPatch(patch);
    engine.noteOn(60, 104);

    double peak = 0.0;
    for (int i = 0; i < 44100; ++i)
    {
        const auto sample = engine.renderSample();
        expect(std::isfinite(sample.left) && std::isfinite(sample.right), "render sample is finite");
        expectNear(sample.left, sample.right, 0.0, "initial engine output is dual mono");
        peak = std::max(peak, static_cast<double>(std::abs(sample.left)));
    }

    expect(peak > 0.001, "engine renders non-silent note");
    expect(peak < 0.98, "engine keeps headroom below full scale");

    engine.noteOff(60);
    for (int i = 0; i < 44100 * 4; ++i)
        engine.renderSample();

    expect(engine.activeVoiceCount() == 0, "released voice is removed");
}

void testVoiceLimit()
{
    opaline::OpalineEngine engine;
    engine.prepare(44100.0, 2);
    engine.setPatch(opaline::OpalinePatch {});
    engine.noteOn(60, 100);
    engine.noteOn(62, 100);
    engine.noteOn(64, 100);
    expect(engine.activeVoiceCount() == 2, "voice count is limited");
}

void testEngineEffectsRendering()
{
    opaline::OpalinePatch patch;
    patch.algorithm = 8;
    patch.feedback = 0;
    patch.effects.reverb = 70;
    patch.effects.mix = 65;
    patch.effects.echoMix = 45;
    patch.effects.tone = 55;
    patch.effects.chorus = 80;
    patch.effects.delay = 35;
    for (auto& op : patch.operators)
    {
        op.level = 0;
        op.envelope.attackRate = 31;
        op.envelope.decay1Rate = 12;
        op.envelope.decay1Level = 15;
        op.envelope.decay2Rate = 0;
        op.envelope.releaseRate = 15;
    }
    patch.operators[0].level = 95;

    opaline::OpalineEngine engine;
    engine.prepare(44100.0, 4);
    engine.setPatch(patch);
    engine.noteOn(60, 104);

    double peak = 0.0;
    for (int i = 0; i < 44100; ++i)
    {
        const auto sample = engine.renderSample();
        expect(std::isfinite(sample.left) && std::isfinite(sample.right), "effect render sample is finite");
        peak = std::max(peak, static_cast<double>(std::abs(sample.left)));
        peak = std::max(peak, static_cast<double>(std::abs(sample.right)));
    }

    expect(peak > 0.001, "effect chain renders non-silent note");
    expect(peak < 0.98, "effect chain keeps headroom below full scale");
}

std::array<std::uint8_t, opaline::kOpalineVmemVoiceSize> makeTestVmem()
{
    std::array<std::uint8_t, opaline::kOpalineVmemVoiceSize> vmem {};
    const std::string name = "TESTVOICE ";
    for (std::size_t i = 0; i < name.size(); ++i)
        vmem.at(57 + i) = static_cast<std::uint8_t>(name[i]);

    vmem[40] = static_cast<std::uint8_t>((6 - 1) | (5 << 3) | 0x40);
    vmem[41] = 22;
    vmem[42] = 33;
    vmem[43] = 44;
    vmem[44] = 55;
    vmem[45] = static_cast<std::uint8_t>(2 | (3 << 2) | (6 << 4));
    vmem[46] = 31;
    vmem[67] = 61;
    vmem[68] = 62;
    vmem[69] = 63;
    vmem[70] = 64;
    vmem[71] = 65;
    vmem[72] = 66;

    for (int block = 0; block < opaline::kOperatorCount; ++block)
    {
        const int base = block * 10;
        vmem.at(static_cast<std::size_t>(base + 0)) = static_cast<std::uint8_t>(10 + block);
        vmem.at(static_cast<std::size_t>(base + 1)) = static_cast<std::uint8_t>(11 + block);
        vmem.at(static_cast<std::size_t>(base + 2)) = static_cast<std::uint8_t>(12 + block);
        vmem.at(static_cast<std::size_t>(base + 3)) = static_cast<std::uint8_t>(13 + block);
        vmem.at(static_cast<std::size_t>(base + 4)) = static_cast<std::uint8_t>(7 + block);
        vmem.at(static_cast<std::size_t>(base + 5)) = static_cast<std::uint8_t>(20 + block);
        vmem.at(static_cast<std::size_t>(base + 6)) = static_cast<std::uint8_t>(0x40 | (block + 1));
        vmem.at(static_cast<std::size_t>(base + 7)) = static_cast<std::uint8_t>(70 + block);
        vmem.at(static_cast<std::size_t>(base + 8)) = static_cast<std::uint8_t>(4 + block);
        vmem.at(static_cast<std::size_t>(base + 9)) = static_cast<std::uint8_t>(((block & 0x03) << 3) | ((block + 3) & 0x07));
    }

    return vmem;
}

std::vector<std::uint8_t> makeTestBulk()
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(opaline::kOpalineBulkMinimumSize), 0);
    bytes.front() = 0xf0u;
    bytes.back() = 0xf7u;

    const auto vmem = makeTestVmem();
    for (int voice = 0; voice < opaline::kOpalineBulkVoiceCount; ++voice)
    {
        const auto offset = static_cast<std::size_t>(opaline::kOpalineBulkVoiceDataOffset
            + voice * opaline::kOpalineVmemVoiceSize);
        for (int i = 0; i < opaline::kOpalineVmemVoiceSize; ++i)
            bytes.at(offset + static_cast<std::size_t>(i)) = vmem.at(static_cast<std::size_t>(i));
    }

    return bytes;
}

void testSysexBulkParsing()
{
    const auto presets = opaline::parseCompatibleBulkVmem(makeTestBulk());
    expect(presets.size() == opaline::kOpalineBulkVoiceCount, "bulk parser returns 32 voices");
    expect(presets[0].name == "TESTVOICE", "bulk parser trims ASCII voice name");
    expect(presets[0].vmem[40] == static_cast<std::uint8_t>((6 - 1) | (5 << 3) | 0x40),
           "bulk parser copies raw VMEM bytes");

    bool threw = false;
    try
    {
        std::vector<std::uint8_t> invalid(12, 0);
        opaline::parseCompatibleBulkVmem(invalid);
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    expect(threw, "bulk parser rejects invalid files");
}

void testVmemDecodeAndPatchMerge()
{
    const auto vmem = makeTestVmem();
    const auto decoded = opaline::decodeCompatibleVmemVoice(vmem);

    expect(decoded.name == "TESTVOICE", "decoder reads voice name");
    expect(decoded.hasVmem, "decoder marks patch as VMEM-backed");
    expect(decoded.patch.algorithm == 6, "decoder reads algorithm");
    expect(decoded.patch.feedback == 5, "decoder reads feedback");
    expect(decoded.patch.lfo.sync, "decoder reads LFO sync");
    expect(decoded.patch.lfo.speed == 22, "decoder reads LFO speed");
    expect(decoded.patch.lfo.delay == 33, "decoder reads LFO delay");
    expect(decoded.patch.lfo.pitchDepth == 44, "decoder reads PMD");
    expect(decoded.patch.lfo.ampDepth == 55, "decoder reads AMD");
    expect(decoded.patch.lfo.wave == 2, "decoder reads LFO wave");
    expect(decoded.patch.lfo.pitchSensitivity == 6, "decoder reads PMS");
    expect(decoded.patch.lfo.ampSensitivity == 3, "decoder reads AMS");
    expect(decoded.patch.transpose == 7, "decoder reads VMEM transpose");
    expect(decoded.patch.pitchEnvelope.rate1 == 61, "decoder reads PEG rate 1");
    expect(decoded.patch.pitchEnvelope.rate2 == 62, "decoder reads PEG rate 2");
    expect(decoded.patch.pitchEnvelope.rate3 == 63, "decoder reads PEG rate 3");
    expect(decoded.patch.pitchEnvelope.level1 == 64, "decoder reads PEG level 1");
    expect(decoded.patch.pitchEnvelope.level2 == 65, "decoder reads PEG level 2");
    expect(decoded.patch.pitchEnvelope.level3 == 66, "decoder reads PEG level 3");

    auto lfoNoiseVmem = vmem;
    lfoNoiseVmem[45] = 115; // S/H wave, AMS=0, PMS=7.
    const auto lfoNoise = opaline::decodeCompatibleVmemVoice(lfoNoiseVmem);
    expect(lfoNoise.patch.lfo.wave == 3, "decoder reads S/H wave from B07-style LFO byte");
    expect(lfoNoise.patch.lfo.ampSensitivity == 0, "decoder reads AMS=0 from B07-style LFO byte");
    expect(lfoNoise.patch.lfo.pitchSensitivity == 7, "decoder reads PMS=7 from B07-style LFO byte");

    expect(decoded.patch.operators[3].envelope.attackRate == 10, "VMEM block 0 maps to OP4");
    expect(decoded.patch.operators[1].envelope.attackRate == 11, "VMEM block 1 maps to OP2");
    expect(decoded.patch.operators[2].envelope.attackRate == 12, "VMEM block 2 maps to OP3");
    expect(decoded.patch.operators[0].envelope.attackRate == 13, "VMEM block 3 maps to OP1");
    expect(decoded.patch.operators[3].ampModEnable, "operator AMS enable is decoded");
    expect(decoded.patch.operators[0].velocity == 4, "operator velocity is decoded");
    expect(decoded.patch.operators[0].level == 73, "operator level is decoded");
    expect(decoded.patch.operators[0].ratioIndex == 7, "operator ratio index is decoded");
    expect(decoded.patch.operators[0].detune == 3, "operator detune is decoded");
    expect(decoded.patch.operators[0].rateScale == 3, "operator rate scale is decoded");

    opaline::OpalinePatch base;
    base.transpose = -7;
    opaline::OpalineVmemPreset preset;
    preset.name = "MERGED";
    preset.vmem = vmem;
    auto merged = opaline::withVmemPreset(base, preset);
    expect(merged.name == "MERGED", "withVmemPreset applies preset name");
    expect(merged.patch.transpose == 7, "withVmemPreset applies VMEM transpose");
    expect(merged.hasVmem, "withVmemPreset keeps raw VMEM backing");

    opaline::clearVmemPreset(merged);
    expect(!merged.hasVmem, "clearVmemPreset removes VMEM backing flag");
    expect(merged.vmem[40] == 0, "clearVmemPreset clears raw VMEM bytes");
}

void testSysexEncoding()
{
    auto decoded = opaline::decodeCompatibleVmemVoice(makeTestVmem());
    decoded.name = "ROUNDTRIP";

    const auto encodedVmem = opaline::encodeCompatibleVmemVoice(decoded);
    const auto roundTrip = opaline::decodeCompatibleVmemVoice(encodedVmem);
    expect(roundTrip.name == "ROUNDTRIP", "VMEM encoder writes voice name");
    expect(roundTrip.patch.algorithm == decoded.patch.algorithm, "VMEM encoder preserves algorithm");
    expect(roundTrip.patch.feedback == decoded.patch.feedback, "VMEM encoder preserves feedback");
    expect(roundTrip.patch.lfo.wave == decoded.patch.lfo.wave, "VMEM encoder preserves LFO wave");
    expect(roundTrip.patch.transpose == decoded.patch.transpose, "VMEM encoder preserves transpose");
    expect(roundTrip.patch.pitchEnvelope.rate1 == decoded.patch.pitchEnvelope.rate1,
           "VMEM encoder preserves PEG rate 1");
    expect(roundTrip.patch.pitchEnvelope.level3 == decoded.patch.pitchEnvelope.level3,
           "VMEM encoder preserves PEG level 3");
    expect(roundTrip.patch.operators[0].level == decoded.patch.operators[0].level,
           "VMEM encoder preserves operator level");

    const std::vector<opaline::OpalinePatchWithMetadata> voices { decoded };
    const auto bulk = opaline::encodeCompatibleBulkVmem(voices);
    expect(bulk.size() == opaline::kOpalineBulkMinimumSize, "bulk encoder writes full compatible bank size");
    expect(bulk.front() == 0xf0u, "bulk encoder writes SysEx start");
    expect(bulk.back() == 0xf7u, "bulk encoder writes SysEx end");

    const auto presets = opaline::parseCompatibleBulkVmem(bulk);
    expect(presets.size() == opaline::kOpalineBulkVoiceCount, "encoded bulk parses as 32 voices");
    expect(presets[0].name == "ROUNDTRIP", "encoded bulk contains first voice name");
    expect(presets[31].name == "ROUNDTRIP", "bulk encoder repeats short banks to 32 voices");
}

void testVoiceLibrary()
{
    const auto library = opaline::makeInitVoiceLibrary();
    expect(library.banks.size() == opaline::kOpalineVoiceBankCount, "voice library has 8 banks");
    expect(library.banks[0].voices.size() == opaline::kOpalineVoiceBankSize, "voice bank has 32 voices");
    expect(opaline::voiceAt(library, 0, 0).name == "INIT 1", "voiceAt reads first init voice");
    expect(opaline::voiceAt(library, 7, 31).name == "INIT 32", "voiceAt reads last init voice");

    const auto imported = opaline::voiceBankFromSysex(makeTestBulk(), "Imported");
    expect(imported.name == "Imported", "voice bank import stores bank name");
    expect(imported.voices[0].name == "TESTVOICE", "voice bank import decodes voice names");
    expect(imported.voices[0].hasVmem, "voice bank import keeps VMEM backing");

    const auto exported = opaline::voiceBankToSysex(imported);
    const auto exportedPresets = opaline::parseCompatibleBulkVmem(exported);
    expect(exportedPresets.size() == opaline::kOpalineVoiceBankSize, "voice bank export writes 32 voices");
    expect(exportedPresets[0].name == "TESTVOICE", "voice bank export preserves voice name");

    bool threw = false;
    try
    {
        (void) opaline::voiceAt(library, 8, 0);
    }
    catch (const std::out_of_range&)
    {
        threw = true;
    }
    expect(threw, "voiceAt rejects invalid bank index");
}

void testOptionalAssetSysex()
{
#ifdef OPALINE_TEST_ASSET_DIR
    const std::string path = std::string(OPALINE_TEST_ASSET_DIR) + "/factory.syx";
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return;

    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto presets = opaline::parseCompatibleBulkVmem(bytes);
    expect(presets.size() == opaline::kOpalineBulkVoiceCount, "assets/factory.syx parses as 32 voices");

    const auto decoded = opaline::decodeCompatibleVmemVoice(presets[0].vmem);
    expect(decoded.hasVmem, "assets/factory.syx first voice keeps VMEM backing");
#endif
}

} // namespace

int main()
{
    testTables();
    testPatchNormalization();
    testEnvelope();
    testFastAttackTiming();
    testD1L15SkipsToDecay2();
    testPitchEnvelope();
    testEngineRendering();
    testVoiceLimit();
    testEngineEffectsRendering();
    testSysexBulkParsing();
    testVmemDecodeAndPatchMerge();
    testSysexEncoding();
    testVoiceLibrary();
    testOptionalAssetSysex();

    if (failures != 0)
    {
        std::cerr << failures << " test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "opaline_engine_tests passed\n";
    return EXIT_SUCCESS;
}
