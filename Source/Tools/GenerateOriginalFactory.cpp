#include "App/OpalineVoiceLibraryXml.h"
#include "Engine/OpalineTables.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using Voice = opaline::OpalinePatchWithMetadata;

int ratioIndex(const double ratio)
{
    const auto& ratios = opaline::opalineRatios();
    const auto found = std::min_element(ratios.begin(), ratios.end(), [ratio](const double lhs, const double rhs)
    {
        return std::abs(lhs - ratio) < std::abs(rhs - ratio);
    });
    return static_cast<int>(std::distance(ratios.begin(), found));
}

Voice beginVoice(const std::string& name, const int algorithm, const int feedback, const int transpose = 0)
{
    Voice voice;
    voice.name = name;
    voice.patch = opaline::OpalinePatch {};
    voice.patch.algorithm = algorithm;
    voice.patch.feedback = feedback;
    voice.patch.transpose = transpose;
    voice.patch.lfo = {};
    voice.patch.pitchEnvelope = {};
    voice.patch.effects = {};
    voice.effectsEnabled = true;

    for (auto& op : voice.patch.operators)
    {
        op = {};
        op.level = 0;
        op.velocity = 0;
        op.ampModEnable = false;
    }
    return voice;
}

void setOperator(Voice& voice,
                 const int index,
                 const double ratio,
                 const int level,
                 const int attack,
                 const int decay1,
                 const int sustainLevel,
                 const int decay2,
                 const int release,
                 const int detune = 0,
                 const int rateScale = 0,
                 const int levelScale = 0,
                 const int velocity = 0,
                 const bool ampMod = false)
{
    auto& op = voice.patch.operators[static_cast<std::size_t>(index)];
    op.ratioIndex = ratioIndex(ratio);
    op.detune = detune;
    op.level = level;
    op.rateScale = rateScale;
    op.levelScale = levelScale;
    op.velocity = velocity;
    op.ampModEnable = ampMod;
    op.enabled = level > 0;
    op.envelope = { attack, decay1, sustainLevel, decay2, release };
}

void setLfo(Voice& voice,
            const int speed,
            const int delay,
            const int pitchDepth,
            const int ampDepth,
            const int pitchSensitivity,
            const int ampSensitivity,
            const int wave = 0)
{
    voice.patch.lfo.speed = speed;
    voice.patch.lfo.delay = delay;
    voice.patch.lfo.pitchDepth = pitchDepth;
    voice.patch.lfo.ampDepth = ampDepth;
    voice.patch.lfo.pitchSensitivity = pitchSensitivity;
    voice.patch.lfo.ampSensitivity = ampSensitivity;
    voice.patch.lfo.wave = wave;
}

void setPitchEnvelope(Voice& voice,
                      const int rate1,
                      const int rate2,
                      const int rate3,
                      const int level1,
                      const int level2,
                      const int level3)
{
    voice.patch.pitchEnvelope = { rate1, rate2, rate3, level1, level2, level3 };
}

void setEffects(Voice& voice,
                const int reverb,
                const int reverbMix,
                const int delay,
                const int delayMix,
                const int chorus,
                const int tone)
{
    voice.patch.effects = { reverb, reverbMix, delayMix, tone, chorus, delay };
    voice.effectsEnabled = reverbMix > 0 || delayMix > 0 || chorus > 0 || tone != 50;
}

std::vector<Voice> makeOriginalVoices()
{
    std::vector<Voice> voices;
    voices.reserve(opaline::kOpalineVoiceBankSize);

    auto add = [&voices](Voice voice)
    {
        voice.patch = opaline::normalizePatch(voice.patch);
        voice.vmem = opaline::encodeCompatibleVmemVoice(voice);
        voice.hasVmem = true;
        voices.push_back(std::move(voice));
    };

    // Keys
    {
        auto v = beginVoice("GlassPiano", 5, 2);
        setOperator(v, 0, 1.00, 94, 31, 15, 6, 7, 6, 0, 1, 18, 3);
        setOperator(v, 1, 3.00, 68, 31, 18, 4, 9, 5, -1, 1, 30, 4);
        setOperator(v, 2, 1.00, 91, 31, 13, 7, 6, 6, 1, 1, 16, 3);
        setOperator(v, 3, 7.00, 55, 31, 20, 2, 12, 5, 1, 2, 42, 4);
        setEffects(v, 46, 20, 0, 0, 7, 56); add(v);
    }
    {
        auto v = beginVoice("SoftTine", 5, 1);
        setOperator(v, 0, 1.00, 91, 31, 12, 8, 4, 7, 0, 1, 20, 4);
        setOperator(v, 1, 4.00, 58, 31, 17, 4, 9, 5, -1, 1, 34, 5);
        setOperator(v, 2, 1.00, 84, 29, 9, 10, 3, 7, 1, 1, 18, 3);
        setOperator(v, 3, 8.00, 46, 31, 21, 2, 15, 5, 1, 2, 50, 5);
        setEffects(v, 52, 24, 14, 6, 12, 60); add(v);
    }
    {
        auto v = beginVoice("WirePiano", 4, 4);
        setOperator(v, 0, 1.00, 95, 31, 18, 4, 10, 6, 0, 1, 22, 3);
        setOperator(v, 1, 2.00, 63, 31, 21, 2, 14, 5, -1, 2, 42, 4);
        setOperator(v, 2, 1.00, 56, 31, 16, 5, 9, 6, 1, 1, 28, 3);
        setOperator(v, 3, 11.00, 49, 31, 23, 1, 17, 4, 1, 2, 56, 4);
        setEffects(v, 40, 17, 10, 4, 8, 64); add(v);
    }
    {
        auto v = beginVoice("DawnBell", 2, 3, 12);
        setOperator(v, 0, 1.00, 89, 31, 10, 5, 5, 8, 0, 1, 15, 3);
        setOperator(v, 1, 2.82, 65, 31, 16, 2, 11, 7, -1, 2, 38, 4);
        setOperator(v, 2, 7.07, 52, 31, 19, 1, 14, 6, 1, 2, 55, 4);
        setOperator(v, 3, 14.10, 44, 31, 22, 0, 18, 5, 1, 3, 72, 3);
        setEffects(v, 68, 32, 20, 9, 6, 66); add(v);
    }
    {
        auto v = beginVoice("PhaseKeys", 6, 2);
        setOperator(v, 0, 1.00, 78, 28, 8, 12, 2, 7, -1, 0, 8, 3);
        setOperator(v, 1, 1.00, 78, 28, 8, 12, 2, 7, 1, 0, 8, 3);
        setOperator(v, 2, 2.00, 62, 30, 12, 8, 5, 6, 0, 1, 24, 4);
        setOperator(v, 3, 3.00, 48, 31, 17, 4, 10, 5, 1, 1, 34, 4);
        setLfo(v, 27, 8, 5, 8, 2, 1, 2);
        setEffects(v, 44, 19, 16, 7, 24, 52); add(v);
    }

    // Bells and mallets
    {
        auto v = beginVoice("SilverBell", 1, 5, 12);
        setOperator(v, 0, 1.00, 88, 31, 8, 3, 4, 8, 0, 1, 24, 3);
        setOperator(v, 1, 2.82, 72, 31, 13, 1, 9, 7, 1, 2, 46, 4);
        setOperator(v, 2, 6.92, 58, 31, 18, 0, 14, 6, -1, 2, 62, 4);
        setOperator(v, 3, 13.84, 46, 31, 22, 0, 18, 5, 1, 3, 78, 3);
        setEffects(v, 74, 36, 24, 11, 5, 70); add(v);
    }
    {
        auto v = beginVoice("CopperMalt", 5, 2);
        setOperator(v, 0, 1.00, 93, 31, 17, 2, 12, 6, 0, 2, 36, 4);
        setOperator(v, 1, 3.00, 67, 31, 22, 0, 17, 5, 1, 2, 58, 4);
        setOperator(v, 2, 2.00, 83, 31, 15, 3, 10, 7, -1, 2, 34, 4);
        setOperator(v, 3, 7.00, 54, 31, 24, 0, 19, 4, 1, 3, 72, 3);
        setEffects(v, 43, 18, 8, 3, 4, 58); add(v);
    }
    {
        auto v = beginVoice("RainMallet", 3, 4, 12);
        setOperator(v, 0, 1.00, 87, 31, 14, 3, 10, 7, 0, 2, 38, 4);
        setOperator(v, 1, 4.24, 63, 31, 21, 0, 16, 5, -1, 2, 62, 5);
        setOperator(v, 2, 9.42, 49, 31, 25, 0, 20, 4, 1, 3, 78, 4);
        setOperator(v, 3, 1.41, 41, 30, 12, 5, 7, 7, 1, 1, 32, 3);
        setEffects(v, 71, 34, 28, 13, 9, 68); add(v);
    }
    {
        auto v = beginVoice("WoodDrop", 4, 1);
        setOperator(v, 0, 1.00, 94, 31, 25, 0, 22, 5, 0, 2, 45, 5);
        setOperator(v, 1, 2.00, 54, 31, 27, 0, 24, 4, 1, 3, 70, 5);
        setOperator(v, 2, 0.50, 59, 31, 23, 0, 20, 5, -1, 2, 52, 4);
        setOperator(v, 3, 5.00, 43, 31, 28, 0, 25, 4, 1, 3, 78, 4);
        setEffects(v, 26, 10, 12, 5, 2, 44); add(v);
    }
    {
        auto v = beginVoice("OrbitChime", 2, 6, 12);
        setOperator(v, 0, 1.00, 82, 31, 7, 6, 3, 9, 0, 1, 18, 3);
        setOperator(v, 1, 5.19, 61, 31, 14, 2, 9, 7, -1, 2, 48, 4);
        setOperator(v, 2, 10.38, 53, 31, 19, 0, 14, 6, 1, 2, 66, 4);
        setOperator(v, 3, 15.70, 42, 31, 23, 0, 18, 5, 1, 3, 82, 3);
        setLfo(v, 18, 16, 7, 3, 3, 1, 1);
        setEffects(v, 76, 38, 34, 16, 8, 72); add(v);
    }
    {
        auto v = beginVoice("QuartzVibe", 5, 2);
        setOperator(v, 0, 1.00, 86, 31, 11, 7, 5, 9, -1, 1, 24, 3);
        setOperator(v, 1, 4.00, 59, 31, 17, 3, 11, 7, 1, 2, 50, 4);
        setOperator(v, 2, 1.00, 86, 31, 11, 7, 5, 9, 1, 1, 24, 3);
        setOperator(v, 3, 6.00, 51, 31, 20, 1, 15, 6, -1, 2, 64, 4);
        setLfo(v, 22, 0, 3, 10, 1, 1, 0);
        setEffects(v, 57, 26, 13, 6, 18, 62); add(v);
    }

    // Strings and pads
    {
        auto v = beginVoice("VelvetStr", 7, 1);
        setOperator(v, 0, 1.00, 73, 22, 5, 14, 2, 8, -1, 0, 10, 2, true);
        setOperator(v, 1, 1.00, 73, 24, 5, 14, 2, 8, 1, 0, 10, 2, true);
        setOperator(v, 2, 1.00, 70, 23, 6, 13, 2, 8, 0, 0, 12, 2, true);
        setOperator(v, 3, 2.00, 42, 25, 8, 11, 4, 7, 1, 1, 26, 3, true);
        setLfo(v, 24, 18, 8, 12, 3, 1, 0);
        setEffects(v, 58, 27, 0, 0, 24, 45); add(v);
    }
    {
        auto v = beginVoice("AirCanvas", 6, 2, 12);
        setOperator(v, 0, 1.00, 64, 18, 4, 15, 1, 9, -1, 0, 8, 2, true);
        setOperator(v, 1, 2.00, 56, 20, 5, 14, 2, 8, 1, 0, 12, 2, true);
        setOperator(v, 2, 3.00, 49, 21, 6, 13, 2, 8, 0, 0, 18, 3, true);
        setOperator(v, 3, 0.50, 38, 17, 4, 15, 1, 10, 1, 0, 10, 2, true);
        setLfo(v, 19, 24, 10, 15, 3, 2, 1);
        setEffects(v, 72, 35, 26, 12, 20, 42); add(v);
    }
    {
        auto v = beginVoice("SlowBloom", 4, 3, 12);
        setOperator(v, 0, 1.00, 69, 16, 4, 15, 1, 10, 0, 0, 8, 2, true);
        setOperator(v, 1, 1.41, 46, 19, 6, 13, 2, 9, -1, 0, 16, 3, true);
        setOperator(v, 2, 2.00, 50, 18, 5, 14, 2, 9, 1, 0, 15, 3, true);
        setOperator(v, 3, 3.00, 39, 21, 7, 12, 3, 8, 1, 1, 24, 3, true);
        setLfo(v, 16, 30, 13, 17, 4, 2, 2);
        setEffects(v, 78, 39, 30, 14, 22, 38); add(v);
    }
    {
        auto v = beginVoice("NightChoir", 5, 1, 12);
        setOperator(v, 0, 1.00, 72, 20, 4, 15, 1, 9, -1, 0, 10, 2, true);
        setOperator(v, 1, 2.00, 45, 23, 6, 13, 3, 8, 1, 0, 18, 3, true);
        setOperator(v, 2, 1.00, 68, 21, 4, 15, 1, 9, 1, 0, 10, 2, true);
        setOperator(v, 3, 3.00, 40, 24, 7, 12, 3, 8, -1, 1, 24, 3, true);
        setLfo(v, 21, 20, 7, 14, 3, 2, 0);
        setEffects(v, 74, 36, 18, 8, 27, 40); add(v);
    }
    {
        auto v = beginVoice("PolarPad", 3, 2, 12);
        setOperator(v, 0, 1.00, 66, 17, 4, 15, 1, 10, 0, 0, 10, 2, true);
        setOperator(v, 1, 2.00, 47, 20, 5, 14, 2, 9, -1, 0, 20, 3, true);
        setOperator(v, 2, 5.00, 36, 22, 7, 12, 3, 8, 1, 1, 32, 3, true);
        setOperator(v, 3, 0.50, 43, 16, 3, 15, 1, 10, 1, 0, 8, 2, true);
        setLfo(v, 14, 35, 16, 12, 4, 1, 1);
        setPitchEnvelope(v, 44, 38, 52, 47, 54, 50);
        setEffects(v, 82, 41, 36, 17, 15, 46); add(v);
    }
    {
        auto v = beginVoice("WarmLayer", 8, 0);
        setOperator(v, 0, 0.50, 58, 24, 5, 14, 2, 8, -1, 0, 6, 2, true);
        setOperator(v, 1, 1.00, 67, 23, 5, 14, 2, 8, 0, 0, 8, 2, true);
        setOperator(v, 2, 2.00, 49, 25, 6, 13, 3, 7, 1, 0, 14, 2, true);
        setOperator(v, 3, 3.00, 39, 26, 8, 11, 4, 7, -1, 1, 22, 3, true);
        setLfo(v, 26, 12, 5, 10, 2, 1, 2);
        setEffects(v, 61, 29, 12, 5, 30, 43); add(v);
    }

    // Brass and reeds
    {
        auto v = beginVoice("BronzeHorn", 3, 4);
        setOperator(v, 0, 1.00, 91, 28, 7, 13, 3, 7, 0, 1, 14, 3);
        setOperator(v, 1, 1.00, 62, 27, 8, 12, 4, 6, -1, 1, 24, 4);
        setOperator(v, 2, 2.00, 54, 29, 10, 10, 5, 6, 1, 1, 30, 4);
        setOperator(v, 3, 3.00, 42, 25, 7, 13, 3, 7, 1, 0, 18, 3);
        setLfo(v, 31, 20, 8, 3, 3, 1, 0);
        setEffects(v, 40, 17, 0, 0, 7, 48); add(v);
    }
    {
        auto v = beginVoice("ClearBrass", 2, 5);
        setOperator(v, 0, 1.00, 93, 30, 8, 12, 4, 7, 0, 1, 16, 4);
        setOperator(v, 1, 2.00, 67, 29, 10, 10, 5, 6, -1, 1, 28, 4);
        setOperator(v, 2, 1.00, 55, 27, 7, 13, 3, 7, 1, 0, 18, 3);
        setOperator(v, 3, 4.00, 48, 31, 13, 7, 7, 5, 1, 1, 38, 4);
        setLfo(v, 34, 16, 10, 2, 3, 1, 0);
        setEffects(v, 36, 15, 0, 0, 5, 56); add(v);
    }
    {
        auto v = beginVoice("MossFlute", 5, 1, 12);
        setOperator(v, 0, 1.00, 87, 26, 5, 14, 2, 9, 0, 0, 8, 2, true);
        setOperator(v, 1, 2.00, 38, 28, 8, 11, 4, 7, -1, 0, 18, 3, true);
        setOperator(v, 2, 1.00, 64, 25, 4, 15, 1, 9, 1, 0, 7, 2, true);
        setOperator(v, 3, 3.00, 31, 29, 10, 9, 5, 6, 1, 1, 30, 3, true);
        setLfo(v, 29, 22, 12, 5, 4, 1, 0);
        setEffects(v, 59, 27, 0, 0, 8, 43); add(v);
    }
    {
        auto v = beginVoice("AmberOboe", 4, 3, 12);
        setOperator(v, 0, 1.00, 89, 29, 6, 14, 2, 8, 0, 1, 10, 3, true);
        setOperator(v, 1, 2.00, 56, 28, 9, 11, 4, 7, -1, 1, 24, 4, true);
        setOperator(v, 2, 1.00, 49, 27, 6, 14, 2, 8, 1, 0, 12, 3, true);
        setOperator(v, 3, 3.00, 43, 30, 11, 9, 5, 6, 1, 1, 34, 4, true);
        setLfo(v, 27, 18, 9, 4, 3, 1, 0);
        setEffects(v, 47, 21, 0, 0, 6, 46); add(v);
    }
    {
        auto v = beginVoice("BreathSax", 3, 4, 12);
        setOperator(v, 0, 1.00, 90, 29, 7, 13, 3, 8, 0, 1, 12, 4, true);
        setOperator(v, 1, 2.00, 60, 28, 10, 10, 5, 7, -1, 1, 28, 4, true);
        setOperator(v, 2, 4.00, 47, 31, 14, 6, 8, 5, 1, 2, 44, 4, true);
        setOperator(v, 3, 1.00, 40, 25, 5, 14, 2, 9, 1, 0, 10, 3, true);
        setLfo(v, 32, 15, 11, 6, 4, 1, 0);
        setEffects(v, 43, 19, 8, 3, 10, 48); add(v);
    }

    // Leads and motion
    {
        auto v = beginVoice("PrismLead", 1, 5, 12);
        setOperator(v, 0, 1.00, 91, 31, 5, 15, 1, 7, 0, 0, 8, 3);
        setOperator(v, 1, 2.00, 58, 31, 8, 12, 4, 6, -1, 0, 18, 4);
        setOperator(v, 2, 3.00, 49, 31, 11, 9, 6, 5, 1, 1, 30, 4);
        setOperator(v, 3, 7.00, 42, 31, 15, 5, 9, 5, 1, 1, 44, 3);
        setLfo(v, 37, 12, 14, 4, 4, 1, 2);
        setEffects(v, 39, 17, 22, 10, 8, 64); add(v);
    }
    {
        auto v = beginVoice("RibbonSyn", 3, 6, 12);
        setOperator(v, 0, 1.00, 88, 30, 6, 14, 2, 8, 0, 0, 10, 3, true);
        setOperator(v, 1, 1.00, 64, 29, 8, 12, 4, 7, -1, 0, 18, 4, true);
        setOperator(v, 2, 2.00, 52, 31, 12, 8, 6, 6, 1, 1, 32, 4, true);
        setOperator(v, 3, 0.50, 43, 27, 5, 14, 2, 9, 1, 0, 9, 3, true);
        setLfo(v, 41, 8, 18, 8, 5, 1, 1);
        setPitchEnvelope(v, 58, 45, 62, 47, 55, 50);
        setEffects(v, 51, 23, 28, 13, 13, 56); add(v);
    }
    {
        auto v = beginVoice("PixelArc", 6, 4, 12);
        setOperator(v, 0, 1.00, 70, 31, 16, 4, 12, 5, -1, 1, 30, 4);
        setOperator(v, 1, 2.00, 62, 31, 18, 3, 14, 5, 1, 1, 38, 4);
        setOperator(v, 2, 5.00, 51, 31, 22, 1, 18, 4, -1, 2, 56, 4);
        setOperator(v, 3, 9.00, 45, 31, 25, 0, 21, 4, 1, 2, 68, 3);
        setLfo(v, 48, 0, 6, 12, 2, 2, 3);
        setEffects(v, 32, 13, 30, 14, 16, 70); add(v);
    }
    {
        auto v = beginVoice("TidalSweep", 2, 5, 12);
        setOperator(v, 0, 0.50, 68, 22, 5, 14, 2, 9, 0, 0, 12, 2, true);
        setOperator(v, 1, 1.00, 54, 24, 7, 12, 3, 8, -1, 0, 22, 3, true);
        setOperator(v, 2, 2.82, 47, 27, 10, 9, 5, 7, 1, 1, 36, 3, true);
        setOperator(v, 3, 7.07, 40, 29, 14, 5, 9, 6, 1, 1, 52, 3, true);
        setLfo(v, 12, 0, 24, 18, 6, 2, 1);
        setPitchEnvelope(v, 36, 28, 44, 43, 61, 50);
        setEffects(v, 80, 40, 42, 20, 18, 50); add(v);
    }

    // Bass and percussion
    {
        auto v = beginVoice("RootBass", 3, 4, -12);
        setOperator(v, 0, 0.50, 96, 31, 9, 11, 5, 7, 0, 1, 6, 4);
        setOperator(v, 1, 1.00, 64, 31, 14, 6, 9, 5, -1, 1, 14, 5);
        setOperator(v, 2, 2.00, 52, 31, 18, 3, 13, 4, 1, 1, 22, 5);
        setOperator(v, 3, 0.50, 42, 29, 10, 10, 5, 6, 1, 0, 8, 3);
        setEffects(v, 12, 4, 0, 0, 2, 38); add(v);
    }
    {
        auto v = beginVoice("RubberBass", 2, 5, -12);
        setOperator(v, 0, 0.50, 95, 31, 12, 8, 7, 6, 0, 1, 8, 5);
        setOperator(v, 1, 1.00, 69, 31, 17, 4, 12, 5, -1, 1, 18, 5);
        setOperator(v, 2, 2.00, 57, 31, 21, 2, 16, 4, 1, 2, 30, 5);
        setOperator(v, 3, 3.00, 44, 31, 24, 0, 20, 4, 1, 2, 42, 4);
        setEffects(v, 10, 3, 0, 0, 5, 48); add(v);
    }
    {
        auto v = beginVoice("PickBass", 4, 3, -12);
        setOperator(v, 0, 0.50, 96, 31, 15, 5, 10, 5, 0, 1, 10, 5);
        setOperator(v, 1, 2.00, 62, 31, 22, 1, 18, 4, -1, 2, 34, 5);
        setOperator(v, 2, 1.00, 58, 31, 18, 3, 14, 5, 1, 1, 22, 4);
        setOperator(v, 3, 5.00, 47, 31, 26, 0, 22, 3, 1, 3, 58, 4);
        setEffects(v, 14, 5, 0, 0, 3, 58); add(v);
    }
    {
        auto v = beginVoice("DeepDrum", 1, 6, -12);
        setOperator(v, 0, 0.50, 94, 31, 24, 0, 21, 5, 0, 2, 30, 5);
        setOperator(v, 1, 1.00, 66, 31, 27, 0, 24, 4, -1, 2, 48, 5);
        setOperator(v, 2, 2.82, 54, 31, 28, 0, 25, 3, 1, 3, 68, 4);
        setOperator(v, 3, 7.07, 43, 31, 30, 0, 27, 3, 1, 3, 82, 3);
        setPitchEnvelope(v, 82, 54, 76, 68, 39, 50);
        setEffects(v, 22, 8, 0, 0, 0, 42); add(v);
    }
    {
        auto v = beginVoice("SnapClack", 6, 7);
        setOperator(v, 0, 1.00, 72, 31, 29, 0, 27, 3, -1, 3, 70, 6);
        setOperator(v, 1, 3.14, 61, 31, 30, 0, 28, 3, 1, 3, 76, 6);
        setOperator(v, 2, 7.85, 53, 31, 31, 0, 29, 2, -1, 3, 84, 5);
        setOperator(v, 3, 14.13, 47, 31, 31, 0, 30, 2, 1, 3, 90, 5);
        setEffects(v, 18, 6, 12, 5, 0, 72); add(v);
    }
    {
        auto v = beginVoice("IronPulse", 8, 0, -12);
        setOperator(v, 0, 0.50, 68, 31, 25, 0, 22, 4, -1, 2, 44, 5);
        setOperator(v, 1, 1.57, 58, 31, 27, 0, 24, 4, 1, 2, 56, 5);
        setOperator(v, 2, 3.46, 51, 31, 29, 0, 26, 3, -1, 3, 70, 4);
        setOperator(v, 3, 8.65, 45, 31, 30, 0, 28, 3, 1, 3, 82, 4);
        setEffects(v, 24, 9, 18, 8, 3, 66); add(v);
    }

    return voices;
}

void validateVoices(const std::vector<Voice>& voices)
{
    if (voices.size() != opaline::kOpalineVoiceBankSize)
        throw std::runtime_error("The original bank must contain exactly 32 voices.");

    std::set<std::string> names;
    for (const auto& voice : voices)
    {
        if (voice.name.empty() || voice.name.size() > 10 || !names.insert(voice.name).second)
            throw std::runtime_error("Voice names must be unique ASCII strings of 1-10 characters.");

        const auto encoded = opaline::encodeCompatibleVmemVoice(voice);
        const auto decoded = opaline::decodeCompatibleVmemVoice(encoded);
        if (decoded.name != voice.name)
            throw std::runtime_error("Voice VMEM round trip failed for " + voice.name);
    }
}

void writeBinary(const juce::File& file, const std::vector<std::uint8_t>& bytes)
{
    if (!file.getParentDirectory().createDirectory())
        throw std::runtime_error("Could not create output directory.");
    if (!file.replaceWithData(bytes.data(), bytes.size()))
        throw std::runtime_error("Could not write " + file.getFullPathName().toStdString());
}

void writeLibraryXml(const juce::File& file, const std::vector<Voice>& voices)
{
    auto library = opaline::makeInitVoiceLibrary();
    library.banks[0].name = "Original Factory v1";
    for (int i = 0; i < opaline::kOpalineVoiceBankSize; ++i)
        library.banks[0].voices[static_cast<std::size_t>(i)] = voices[static_cast<std::size_t>(i)];

    auto xml = opalineapp::voiceLibraryToXml(library);
    xml->setAttribute("source", "factory_original_v1.syx");
    for (int bankIndex = 1; bankIndex < opaline::kOpalineVoiceBankCount; ++bankIndex)
    {
        if (auto* bank = xml->getChildElement(bankIndex))
            bank->deleteAllChildElements();
    }

    opaline::OpalineVoiceLibrary restored;
    if (!opalineapp::voiceLibraryFromXml(*xml, restored))
        throw std::runtime_error("Generated library XML could not be decoded.");
    for (int i = 0; i < opaline::kOpalineVoiceBankSize; ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        if (opaline::encodeCompatibleVmemVoice(restored.banks[0].voices[index])
            != opaline::encodeCompatibleVmemVoice(voices[index]))
        {
            throw std::runtime_error("Generated XML voice does not match SysEx voice "
                                     + std::to_string(i + 1));
        }
    }

    if (!file.getParentDirectory().createDirectory())
        throw std::runtime_error("Could not create output directory.");
    if (!file.replaceWithText(xml->toString(), false, false, "\n"))
        throw std::runtime_error("Could not write " + file.getFullPathName().toStdString());
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const juce::File outputDirectory = argc >= 2
            ? juce::File(juce::String::fromUTF8(argv[1]))
            : juce::File::getCurrentWorkingDirectory().getChildFile("assets");
        const auto voices = makeOriginalVoices();
        validateVoices(voices);

        const auto sysex = opaline::encodeCompatibleBulkVmem(voices);
        const auto parsed = opaline::parseCompatibleBulkVmem(sysex);
        if (parsed.size() != voices.size())
            throw std::runtime_error("Bulk SysEx validation failed.");

        writeBinary(outputDirectory.getChildFile("factory_original_v1.syx"), sysex);
        writeLibraryXml(outputDirectory.getChildFile("factory_original_v1.opalinelibrary.xml"), voices);

        std::cout << "Generated 32 independently designed voices in "
                  << outputDirectory.getFullPathName() << '\n';
        for (std::size_t i = 0; i < voices.size(); ++i)
            std::cout << (i + 1) << ". " << voices[i].name << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
