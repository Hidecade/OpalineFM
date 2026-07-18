#import "OpalineMobileEngineBridge.h"
#import "OpalineMobileVoiceLibraryXML.h"

#include "Engine/ChipVoiceImport.h"
#include "Engine/OpalineEngine.h"
#include "Engine/RealtimeCommandQueue.h"
#include "Engine/OpalineSysex.h"
#include "Engine/OpalineTables.h"
#include "Engine/OpalineVoiceLibrary.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

namespace
{
constexpr int kPreparedScratchFrames = 4096;

enum class RealtimeCommandType
{
    noteOn,
    noteOff,
    pitchBend,
    modWheel,
    sustainPedal,
    portamentoFootSwitch,
    patchParameter,
    operatorParameter,
    operatorEnabled,
    operatorAmpModEnabled,
    effectsEnabled,
    pitchBendRange,
    portamento,
    portamentoModeA,
    portamentoModeB,
    modWheelRanges,
    monoModeA,
    monoModeB,
    dualDetune,
    splitPoint,
    abBalance,
    performanceMode
};

enum class PatchParameterId
{
    invalid,
    algorithm,
    feedback,
    pitchRate1,
    pitchRate2,
    pitchRate3,
    pitchLevel1,
    pitchLevel2,
    pitchLevel3,
    lfoSpeed,
    lfoDelay,
    lfoPitchDepth,
    lfoAmpDepth,
    lfoPitchSensitivity,
    lfoAmpSensitivity,
    lfoWave,
    lfoSync,
    reverb,
    delay,
    chorus,
    reverbMix,
    delayMix,
    tone
};

enum class OperatorParameterId
{
    invalid,
    ratio,
    detune,
    level,
    rateScale,
    levelScale,
    velocity,
    attackRate,
    decay1Rate,
    decay1Level,
    decay2Rate,
    releaseRate
};

struct RealtimeCommand
{
    RealtimeCommandType type {};
    int value1 = 0;
    int value2 = 0;
    double normalizedValue = 0.0;
    int value3 = 0;
};

constexpr std::size_t kRealtimeCommandCapacity = 1024;

void enqueueRealtimeCommand(opaline::RealtimeCommandQueue<RealtimeCommand, kRealtimeCommandCapacity>& queue,
                            std::atomic<bool>& overflowed,
                            const RealtimeCommand& command) noexcept
{
    if (!queue.push(command))
        overflowed.store(true, std::memory_order_release);
}
}

@interface OpalineMobileEngineBridge ()
{
    std::unique_ptr<opaline::OpalineEngine> engine;
    std::unique_ptr<opaline::OpalineEngine> engineB;
    opaline::OpalineVoiceLibrary library;
    opaline::OpalinePatch currentPatch;
    opaline::OpalinePatch currentPatchB;
    opaline::OpalinePatchWithMetadata copiedVoice;
    std::string currentVoiceName;
    std::string currentVoiceBName;
    std::vector<float> scratchLeft;
    std::vector<float> scratchRight;
    std::vector<float> scratchBLeft;
    std::vector<float> scratchBRight;
    std::array<std::atomic<float>, 4096> scopeSamples;
    std::atomic<int> scopeWriteIndex;
    std::atomic<int> scopeTriggerNote;
    std::atomic<double> scopeSampleRate;
    std::array<int, 128> scopeHeldNoteCounts;
    int scopeHeldNoteTotal;
    std::array<float, 128> scopeSmoothedDisplay;
    int scopeSmoothedNote;
    BOOL scopeHasSmoothedDisplay;
    opaline::RealtimeCommandQueue<RealtimeCommand, kRealtimeCommandCapacity> realtimeCommands;
    std::atomic<bool> realtimeCommandOverflowed;
    std::mutex engineMutex;
    double currentSampleRate;
    std::atomic<double> currentPitchBend;
    std::atomic<double> currentModWheel;
    int currentBank;
    int currentVoice;
    int currentVoiceB;
    int performanceMode;
    int dualDetune;
    int splitPoint;
    int abBalance;
    int pitchBendRange;
    int portamento;
    int portamentoModeA;
    int portamentoModeB;
    int modWheelPitchRange;
    int modWheelAmpRange;
    BOOL effectsEnabled;
    BOOL monoA;
    BOOL monoB;
    BOOL hasCopiedPatch;
    BOOL didLoadInitialFactoryBank;
}
- (void)prepareEnginesNoLock;
- (void)applyPatchesNoLock;
- (void)applyPitchBendNoLock;
- (void)applyCurrentPatchNoLock;
- (void)syncCurrentVoiceToLibraryNoLock;
- (void)mixEngineBNoLockLeft:(float*)left right:(float*)right frames:(int)frames;
- (void)pushScopeSamplesNoLock:(const float*)left frames:(int)frames;
- (void)applyRealtimeCommandsNoLock;
@end

static std::string xmlEscaped(const std::string& text)
{
    std::string result;
    result.reserve(text.size());
    for (const char ch : text)
    {
        switch (ch)
        {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&apos;"; break;
            default: result += ch; break;
        }
    }
    return result;
}

static std::string encodedVoiceData(const opaline::OpalinePatchWithMetadata& voice)
{
    static constexpr char encodingTable[] = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+";
    const auto vmem = opaline::encodeCompatibleVmemVoice(voice);
    std::string encoded = std::to_string(vmem.size()) + ".";
    const auto characterCount = (vmem.size() * 8 + 5) / 6;
    encoded.reserve(encoded.size() + characterCount);
    for (std::size_t character = 0; character < characterCount; ++character)
    {
        int value = 0;
        for (int bit = 0; bit < 6; ++bit)
        {
            const auto bitPosition = character * 6 + static_cast<std::size_t>(bit);
            if (bitPosition >= vmem.size() * 8)
                break;
            value |= ((vmem[bitPosition / 8] >> (bitPosition % 8)) & 1) << bit;
        }
        encoded += encodingTable[value];
    }
    return encoded;
}

static PatchParameterId patchParameterId(NSString* parameter)
{
    if ([parameter isEqualToString:@"alg"]) return PatchParameterId::algorithm;
    if ([parameter isEqualToString:@"fb"]) return PatchParameterId::feedback;
    if ([parameter isEqualToString:@"pr1"]) return PatchParameterId::pitchRate1;
    if ([parameter isEqualToString:@"pr2"]) return PatchParameterId::pitchRate2;
    if ([parameter isEqualToString:@"pr3"]) return PatchParameterId::pitchRate3;
    if ([parameter isEqualToString:@"pl1"]) return PatchParameterId::pitchLevel1;
    if ([parameter isEqualToString:@"pl2"]) return PatchParameterId::pitchLevel2;
    if ([parameter isEqualToString:@"pl3"]) return PatchParameterId::pitchLevel3;
    if ([parameter isEqualToString:@"lfo.speed"]) return PatchParameterId::lfoSpeed;
    if ([parameter isEqualToString:@"lfo.delay"]) return PatchParameterId::lfoDelay;
    if ([parameter isEqualToString:@"lfo.pmd"]) return PatchParameterId::lfoPitchDepth;
    if ([parameter isEqualToString:@"lfo.amd"]) return PatchParameterId::lfoAmpDepth;
    if ([parameter isEqualToString:@"lfo.pms"]) return PatchParameterId::lfoPitchSensitivity;
    if ([parameter isEqualToString:@"lfo.ams"]) return PatchParameterId::lfoAmpSensitivity;
    if ([parameter isEqualToString:@"lfo.wave"]) return PatchParameterId::lfoWave;
    if ([parameter isEqualToString:@"lfo.sync"]) return PatchParameterId::lfoSync;
    if ([parameter isEqualToString:@"fx.reverb"]) return PatchParameterId::reverb;
    if ([parameter isEqualToString:@"fx.delay"]) return PatchParameterId::delay;
    if ([parameter isEqualToString:@"fx.chorus"]) return PatchParameterId::chorus;
    if ([parameter isEqualToString:@"fx.revmix"]) return PatchParameterId::reverbMix;
    if ([parameter isEqualToString:@"fx.dlymix"]) return PatchParameterId::delayMix;
    if ([parameter isEqualToString:@"fx.tone"]) return PatchParameterId::tone;
    return PatchParameterId::invalid;
}

static OperatorParameterId operatorParameterId(NSString* parameter)
{
    if ([parameter isEqualToString:@"ratio"]) return OperatorParameterId::ratio;
    if ([parameter isEqualToString:@"detune"]) return OperatorParameterId::detune;
    if ([parameter isEqualToString:@"level"]) return OperatorParameterId::level;
    if ([parameter isEqualToString:@"ratesc"]) return OperatorParameterId::rateScale;
    if ([parameter isEqualToString:@"levelsc"]) return OperatorParameterId::levelScale;
    if ([parameter isEqualToString:@"vel"]) return OperatorParameterId::velocity;
    if ([parameter isEqualToString:@"ar"]) return OperatorParameterId::attackRate;
    if ([parameter isEqualToString:@"d1r"]) return OperatorParameterId::decay1Rate;
    if ([parameter isEqualToString:@"d1l"]) return OperatorParameterId::decay1Level;
    if ([parameter isEqualToString:@"d2r"]) return OperatorParameterId::decay2Rate;
    if ([parameter isEqualToString:@"rr"]) return OperatorParameterId::releaseRate;
    return OperatorParameterId::invalid;
}

static bool readBundledFactoryBank(opaline::OpalineVoiceBank& bank)
{
    NSURL* libraryURL = [NSBundle.mainBundle URLForResource:@"factory.opalinelibrary" withExtension:@"xml"];
    NSData* libraryData = libraryURL != nil ? [NSData dataWithContentsOfURL:libraryURL] : nil;
    opaline::OpalineVoiceLibrary factoryLibrary;
    if (opaline::mobile::voiceLibraryFromXMLData(libraryData, factoryLibrary))
    {
        bank = std::move(factoryLibrary.banks[0]);
        return true;
    }

    NSString* path = [NSBundle.mainBundle pathForResource:@"factory" ofType:@"syx"];
    if (path == nil)
        return false;

    NSData* data = [NSData dataWithContentsOfFile:path];
    if (data == nil || data.length == 0)
        return false;

    const auto* bytes = static_cast<const std::uint8_t*>(data.bytes);
    std::vector<std::uint8_t> sysex(bytes, bytes + data.length);
    try
    {
        bank = opaline::voiceBankFromSysex(sysex, "Factory");
    }
    catch (...)
    {
        bank = opaline::makeInitVoiceBank("Factory");
    }
    return true;
}

@implementation OpalineMobileEngineBridge

- (instancetype)init
{
    self = [super init];
    if (self)
    {
        engine = std::make_unique<opaline::OpalineEngine>();
        engineB = std::make_unique<opaline::OpalineEngine>();
        library = opaline::makeInitVoiceLibrary();
        currentPatch = opaline::normalizePatch(opaline::voiceAt(library, 0, 0).patch);
        currentPatchB = opaline::normalizePatch(opaline::voiceAt(library, 0, 16).patch);
        currentVoiceName = opaline::voiceAt(library, 0, 0).name;
        currentVoiceBName = opaline::voiceAt(library, 0, 16).name;
        currentSampleRate = 44100.0;
        currentPitchBend.store(0.0, std::memory_order_relaxed);
        currentModWheel.store(0.0, std::memory_order_relaxed);
        currentBank = 0;
        currentVoice = 0;
        currentVoiceB = 16;
        performanceMode = 0;
        dualDetune = 0;
        splitPoint = 60;
        abBalance = 0;
        pitchBendRange = 2;
        portamento = 0;
        portamentoModeA = 0;
        portamentoModeB = 0;
        modWheelPitchRange = 0;
        modWheelAmpRange = 0;
        effectsEnabled = YES;
        monoA = NO;
        monoB = NO;
        hasCopiedPatch = NO;
        didLoadInitialFactoryBank = NO;
        scopeWriteIndex.store(0, std::memory_order_relaxed);
        scopeTriggerNote.store(-1, std::memory_order_relaxed);
        scopeSampleRate.store(currentSampleRate, std::memory_order_relaxed);
        scopeHeldNoteCounts.fill(0);
        scopeHeldNoteTotal = 0;
        scopeSmoothedDisplay.fill(0.0f);
        scopeSmoothedNote = -1;
        scopeHasSmoothedDisplay = NO;
        realtimeCommandOverflowed.store(false, std::memory_order_relaxed);
        for (auto& sample : scopeSamples)
            sample.store(0.0f, std::memory_order_relaxed);
    }
    return self;
}

- (void)prepareWithSampleRate:(double)sampleRate maxVoices:(int)maxVoices
{
    std::lock_guard<std::mutex> lock(engineMutex);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    scopeSampleRate.store(currentSampleRate, std::memory_order_release);
    [self prepareEnginesNoLock];
    if (!didLoadInitialFactoryBank)
    {
        [self loadBundledFactoryBank];
        didLoadInitialFactoryBank = YES;
    }
    [self applyPatchesNoLock];
}

- (void)selectVoiceBank:(int)bank voice:(int)voice
{
    std::lock_guard<std::mutex> lock(engineMutex);
    [self applyRealtimeCommandsNoLock];
    [self syncCurrentVoiceToLibraryNoLock];
    currentBank = std::max(0, std::min(bank, opaline::kOpalineVoiceBankCount - 1));
    currentVoice = std::max(0, std::min(voice, opaline::kOpalineVoiceBankSize - 1));
    currentPatch = opaline::normalizePatch(opaline::voiceAt(library, currentBank, currentVoice).patch);
    currentPatchB = opaline::normalizePatch(opaline::voiceAt(library, currentBank, currentVoiceB).patch);
    currentVoiceName = opaline::voiceAt(library, currentBank, currentVoice).name;
    currentVoiceBName = opaline::voiceAt(library, currentBank, currentVoiceB).name;
    effectsEnabled = opaline::voiceAt(library, currentBank, currentVoice).effectsEnabled;
    [self applyCurrentPatchNoLock];
}

- (void)selectVoiceB:(int)voice
{
    std::lock_guard<std::mutex> lock(engineMutex);
    [self applyRealtimeCommandsNoLock];
    currentVoiceB = std::max(0, std::min(voice, opaline::kOpalineVoiceBankSize - 1));
    currentPatchB = opaline::normalizePatch(opaline::voiceAt(library, currentBank, currentVoiceB).patch);
    currentVoiceBName = opaline::voiceAt(library, currentBank, currentVoiceB).name;
    [self applyCurrentPatchNoLock];
}

- (NSString*)currentVoiceName
{
    std::string name;
    {
        std::lock_guard<std::mutex> lock(engineMutex);
        name = currentVoiceName;
    }
    return [NSString stringWithUTF8String:name.c_str()];
}

- (NSString*)currentVoiceBName
{
    std::string name;
    {
        std::lock_guard<std::mutex> lock(engineMutex);
        name = currentVoiceBName;
    }
    return [NSString stringWithUTF8String:name.c_str()];
}

- (NSArray<NSString*>*)voiceBankNames
{
    std::array<std::string, opaline::kOpalineVoiceBankCount> nameSnapshot;
    {
        std::lock_guard<std::mutex> lock(engineMutex);
        for (int bank = 0; bank < opaline::kOpalineVoiceBankCount; ++bank)
            nameSnapshot[static_cast<std::size_t>(bank)] = library.banks[static_cast<std::size_t>(bank)].name;
    }

    NSMutableArray<NSString*>* names = [NSMutableArray arrayWithCapacity:opaline::kOpalineVoiceBankCount];
    for (int bank = 0; bank < opaline::kOpalineVoiceBankCount; ++bank)
    {
        const auto& name = nameSnapshot[static_cast<std::size_t>(bank)];
        const std::string displayName = name.empty() ? "Bank " + std::to_string(bank + 1) : name;
        [names addObject:[NSString stringWithUTF8String:displayName.c_str()]];
    }
    return names;
}

- (NSArray<NSString*>*)voiceNamesForBank:(int)bank
{
    const int safeBank = std::max(0, std::min(bank, opaline::kOpalineVoiceBankCount - 1));
    std::array<std::string, opaline::kOpalineVoiceBankSize> nameSnapshot;
    {
        std::lock_guard<std::mutex> lock(engineMutex);
        for (int voice = 0; voice < opaline::kOpalineVoiceBankSize; ++voice)
            nameSnapshot[static_cast<std::size_t>(voice)] = opaline::voiceAt(library, safeBank, voice).name;
    }

    NSMutableArray<NSString*>* names = [NSMutableArray arrayWithCapacity:opaline::kOpalineVoiceBankSize];
    for (int voice = 0; voice < opaline::kOpalineVoiceBankSize; ++voice)
    {
        const auto& name = nameSnapshot[static_cast<std::size_t>(voice)];
        [names addObject:[NSString stringWithUTF8String:name.c_str()]];
    }
    return names;
}

- (BOOL)loadVoiceBankData:(NSData*)data name:(NSString*)name
{
    if (data == nil || data.length == 0)
        return NO;

    const auto* bytes = static_cast<const std::uint8_t*>(data.bytes);
    std::vector<std::uint8_t> sysex(bytes, bytes + data.length);
    try
    {
        const std::string bankName = name != nil && name.length > 0
            ? std::string(name.UTF8String)
            : "Imported";
        auto importedBank = opaline::voiceBankFromSysex(sysex, bankName);

        std::lock_guard<std::mutex> lock(engineMutex);
        [self applyRealtimeCommandsNoLock];
        engine->panic();
        engineB->panic();
        library.banks[static_cast<std::size_t>(currentBank)] = std::move(importedBank);
        currentVoice = std::max(0, std::min(currentVoice, opaline::kOpalineVoiceBankSize - 1));
        currentVoiceB = std::max(0, std::min(currentVoiceB, opaline::kOpalineVoiceBankSize - 1));
        currentPatch = opaline::normalizePatch(opaline::voiceAt(library, currentBank, currentVoice).patch);
        currentPatchB = opaline::normalizePatch(opaline::voiceAt(library, currentBank, currentVoiceB).patch);
        currentVoiceName = opaline::voiceAt(library, currentBank, currentVoice).name;
        currentVoiceBName = opaline::voiceAt(library, currentBank, currentVoiceB).name;
        [self applyCurrentPatchNoLock];
        return YES;
    }
    catch (...)
    {
        return NO;
    }
}

- (NSData*)currentVoiceBankSysexData
{
    try
    {
        opaline::OpalineVoiceBank bankSnapshot;
        {
            std::lock_guard<std::mutex> lock(engineMutex);
            [self applyRealtimeCommandsNoLock];
            [self syncCurrentVoiceToLibraryNoLock];
            bankSnapshot = library.banks[static_cast<std::size_t>(currentBank)];
        }
        const auto bytes = opaline::voiceBankToSysex(bankSnapshot);
        return [NSData dataWithBytes:bytes.data() length:bytes.size()];
    }
    catch (...)
    {
        return [NSData data];
    }
}

- (NSData*)voiceLibraryXMLData
{
    opaline::OpalineVoiceLibrary librarySnapshot;
    {
        std::lock_guard<std::mutex> lock(engineMutex);
        [self applyRealtimeCommandsNoLock];
        [self syncCurrentVoiceToLibraryNoLock];
        librarySnapshot = library;
    }

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<compatibleVoiceLibrary version=\"2\">\n";
    for (int bankIndex = 0; bankIndex < opaline::kOpalineVoiceBankCount; ++bankIndex)
    {
        const auto& bank = librarySnapshot.banks[static_cast<std::size_t>(bankIndex)];
        xml << "  <Bank index=\"" << bankIndex << "\" name=\"" << xmlEscaped(bank.name) << "\">\n";
        for (int voiceIndex = 0; voiceIndex < opaline::kOpalineVoiceBankSize; ++voiceIndex)
        {
            const auto& voice = bank.voices[static_cast<std::size_t>(voiceIndex)];
            const auto encoded = encodedVoiceData(voice);
            const auto effects = opaline::normalizePatch(voice.patch).effects;
            xml << "    <Voice index=\"" << voiceIndex << "\" name=\"" << xmlEscaped(voice.name) << "\" vmem=\""
                << encoded << "\">\n";
            xml << "      <Effects enabled=\"" << (voice.effectsEnabled ? 1 : 0)
                << "\" reverb=\"" << effects.reverb
                << "\" mix=\"" << effects.mix
                << "\" delay=\"" << effects.delay
                << "\" echoMix=\"" << effects.echoMix
                << "\" chorus=\"" << effects.chorus
                << "\" tone=\"" << effects.tone << "\"/>\n";
            xml << "    </Voice>\n";
        }
        xml << "  </Bank>\n";
    }
    xml << "</compatibleVoiceLibrary>\n";
    const auto text = xml.str();
    return [NSData dataWithBytes:text.data() length:text.size()];
}

- (BOOL)loadVoiceLibraryXMLData:(NSData*)data
{
    opaline::OpalineVoiceLibrary restored;
    if (!opaline::mobile::voiceLibraryFromXMLData(data, restored))
        return NO;

    std::lock_guard<std::mutex> lock(engineMutex);
    [self applyRealtimeCommandsNoLock];
    engine->panic();
    engineB->panic();
    library = std::move(restored);
    [self applyPatchesNoLock];
    return YES;
}

- (void)reloadBundledFactoryBank
{
    opaline::OpalineVoiceBank factoryBank;
    if (!readBundledFactoryBank(factoryBank))
        return;

    std::lock_guard<std::mutex> lock(engineMutex);
    [self applyRealtimeCommandsNoLock];
    library.banks[0] = std::move(factoryBank);
    currentPatch = opaline::normalizePatch(opaline::voiceAt(library, currentBank, currentVoice).patch);
    currentPatchB = opaline::normalizePatch(opaline::voiceAt(library, currentBank, currentVoiceB).patch);
    currentVoiceName = opaline::voiceAt(library, currentBank, currentVoice).name;
    currentVoiceBName = opaline::voiceAt(library, currentBank, currentVoiceB).name;
    effectsEnabled = opaline::voiceAt(library, currentBank, currentVoice).effectsEnabled;
    engine->panic();
    engineB->panic();
    [self applyCurrentPatchNoLock];
}

- (NSData*)currentSingleVoiceXMLData
{
    opaline::OpalinePatch patchSnapshot;
    std::string voiceNameSnapshot;
    BOOL effectsEnabledSnapshot = NO;
    {
        std::lock_guard<std::mutex> lock(engineMutex);
        [self applyRealtimeCommandsNoLock];
        patchSnapshot = currentPatch;
        voiceNameSnapshot = currentVoiceName;
        effectsEnabledSnapshot = effectsEnabled;
    }

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<opalineVoice version=\"1\" name=\"" << xmlEscaped(voiceNameSnapshot) << "\">\n";
    xml << "  <Patch>\n";
    xml << "    <Parameter key=\"alg\" value=\"" << patchSnapshot.algorithm << "\"/>\n";
    xml << "    <Parameter key=\"fb\" value=\"" << patchSnapshot.feedback << "\"/>\n";
    xml << "    <Parameter key=\"pr1\" value=\"" << patchSnapshot.pitchEnvelope.rate1 << "\"/>\n";
    xml << "    <Parameter key=\"pr2\" value=\"" << patchSnapshot.pitchEnvelope.rate2 << "\"/>\n";
    xml << "    <Parameter key=\"pr3\" value=\"" << patchSnapshot.pitchEnvelope.rate3 << "\"/>\n";
    xml << "    <Parameter key=\"pl1\" value=\"" << patchSnapshot.pitchEnvelope.level1 << "\"/>\n";
    xml << "    <Parameter key=\"pl2\" value=\"" << patchSnapshot.pitchEnvelope.level2 << "\"/>\n";
    xml << "    <Parameter key=\"pl3\" value=\"" << patchSnapshot.pitchEnvelope.level3 << "\"/>\n";
    xml << "    <Parameter key=\"lfo.speed\" value=\"" << patchSnapshot.lfo.speed << "\"/>\n";
    xml << "    <Parameter key=\"lfo.delay\" value=\"" << patchSnapshot.lfo.delay << "\"/>\n";
    xml << "    <Parameter key=\"lfo.pmd\" value=\"" << patchSnapshot.lfo.pitchDepth << "\"/>\n";
    xml << "    <Parameter key=\"lfo.amd\" value=\"" << patchSnapshot.lfo.ampDepth << "\"/>\n";
    xml << "    <Parameter key=\"lfo.pms\" value=\"" << patchSnapshot.lfo.pitchSensitivity << "\"/>\n";
    xml << "    <Parameter key=\"lfo.ams\" value=\"" << patchSnapshot.lfo.ampSensitivity << "\"/>\n";
    xml << "    <Parameter key=\"lfo.wave\" value=\"" << patchSnapshot.lfo.wave << "\"/>\n";
    xml << "    <Parameter key=\"lfo.sync\" value=\"" << (patchSnapshot.lfo.sync ? 1 : 0) << "\"/>\n";
    xml << "    <Parameter key=\"fx.reverb\" value=\"" << patchSnapshot.effects.reverb << "\"/>\n";
    xml << "    <Parameter key=\"fx.delay\" value=\"" << patchSnapshot.effects.delay << "\"/>\n";
    xml << "    <Parameter key=\"fx.chorus\" value=\"" << patchSnapshot.effects.chorus << "\"/>\n";
    xml << "    <Parameter key=\"fx.revmix\" value=\"" << patchSnapshot.effects.mix << "\"/>\n";
    xml << "    <Parameter key=\"fx.dlymix\" value=\"" << patchSnapshot.effects.echoMix << "\"/>\n";
    xml << "    <Parameter key=\"fx.tone\" value=\"" << patchSnapshot.effects.tone << "\"/>\n";
    xml << "    <Parameter key=\"fx.enabled\" value=\"" << (effectsEnabledSnapshot ? 1 : 0) << "\"/>\n";

    for (int opIndex = 0; opIndex < opaline::kOperatorCount; ++opIndex)
    {
        const auto& op = patchSnapshot.operators[static_cast<std::size_t>(opIndex)];
        xml << "    <Operator index=\"" << opIndex << "\" ratio=\"" << op.ratioIndex
            << "\" detune=\"" << op.detune
            << "\" level=\"" << op.level
            << "\" ratesc=\"" << op.rateScale
            << "\" levelsc=\"" << op.levelScale
            << "\" vel=\"" << op.velocity
            << "\" ar=\"" << op.envelope.attackRate
            << "\" d1r=\"" << op.envelope.decay1Rate
            << "\" d1l=\"" << op.envelope.decay1Level
            << "\" d2r=\"" << op.envelope.decay2Rate
            << "\" rr=\"" << op.envelope.releaseRate
            << "\" enabled=\"" << (op.enabled ? 1 : 0)
            << "\" am=\"" << (op.ampModEnable ? 1 : 0)
            << "\"/>\n";
    }

    xml << "  </Patch>\n";
    xml << "</opalineVoice>\n";
    const auto text = xml.str();
    return [NSData dataWithBytes:text.data() length:text.size()];
}

- (BOOL)loadSingleVoiceXMLData:(NSData*)data fallbackName:(NSString*)name
{
    if (data == nil || data.length == 0)
        return NO;

    NSString* xml = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if (xml == nil || [xml rangeOfString:@"<opalineVoice"].location == NSNotFound)
        return NO;

    opaline::OpalinePatch patch;
    BOOL importedEffectsEnabled = NO;
    {
        std::lock_guard<std::mutex> lock(engineMutex);
        [self applyRealtimeCommandsNoLock];
        patch = currentPatch;
        importedEffectsEnabled = effectsEnabled;
    }

    const auto clamp = [](const int v, const int low, const int high) { return std::max(low, std::min(v, high)); };
    const auto applyParameter = [&](NSString* key, const int value)
    {
        if ([key isEqualToString:@"alg"])
            patch.algorithm = clamp(value, 1, 8);
        else if ([key isEqualToString:@"fb"])
            patch.feedback = clamp(value, 0, 7);
        else if ([key isEqualToString:@"pr1"])
            patch.pitchEnvelope.rate1 = clamp(value, 0, 99);
        else if ([key isEqualToString:@"pr2"])
            patch.pitchEnvelope.rate2 = clamp(value, 0, 99);
        else if ([key isEqualToString:@"pr3"])
            patch.pitchEnvelope.rate3 = clamp(value, 0, 99);
        else if ([key isEqualToString:@"pl1"])
            patch.pitchEnvelope.level1 = clamp(value, 0, 99);
        else if ([key isEqualToString:@"pl2"])
            patch.pitchEnvelope.level2 = clamp(value, 0, 99);
        else if ([key isEqualToString:@"pl3"])
            patch.pitchEnvelope.level3 = clamp(value, 0, 99);
        else if ([key isEqualToString:@"lfo.speed"])
            patch.lfo.speed = clamp(value, 0, 99);
        else if ([key isEqualToString:@"lfo.delay"])
            patch.lfo.delay = clamp(value, 0, 99);
        else if ([key isEqualToString:@"lfo.pmd"])
            patch.lfo.pitchDepth = clamp(value, 0, 99);
        else if ([key isEqualToString:@"lfo.amd"])
            patch.lfo.ampDepth = clamp(value, 0, 99);
        else if ([key isEqualToString:@"lfo.pms"])
            patch.lfo.pitchSensitivity = clamp(value, 0, 7);
        else if ([key isEqualToString:@"lfo.ams"])
            patch.lfo.ampSensitivity = clamp(value, 0, 3);
        else if ([key isEqualToString:@"lfo.wave"])
            patch.lfo.wave = clamp(value, 0, 3);
        else if ([key isEqualToString:@"lfo.sync"])
            patch.lfo.sync = value != 0;
        else if ([key isEqualToString:@"fx.reverb"])
            patch.effects.reverb = clamp(value, 0, 99);
        else if ([key isEqualToString:@"fx.delay"])
            patch.effects.delay = clamp(value, 0, 99);
        else if ([key isEqualToString:@"fx.chorus"])
            patch.effects.chorus = clamp(value, 0, 99);
        else if ([key isEqualToString:@"fx.revmix"])
            patch.effects.mix = clamp(value, 0, 99);
        else if ([key isEqualToString:@"fx.dlymix"])
            patch.effects.echoMix = clamp(value, 0, 99);
        else if ([key isEqualToString:@"fx.tone"])
            patch.effects.tone = clamp(value, 0, 99);
        else if ([key isEqualToString:@"fx.enabled"])
            importedEffectsEnabled = value != 0;
    };

    NSError* regexError = nil;
    NSRegularExpression* parameterRegex = [NSRegularExpression regularExpressionWithPattern:@"<Parameter\\s+key=\"([^\"]+)\"\\s+value=\"(-?\\d+)\"\\s*/>"
                                                                                   options:0
                                                                                     error:&regexError];
    for (NSTextCheckingResult* match in [parameterRegex matchesInString:xml options:0 range:NSMakeRange(0, xml.length)])
    {
        if (match.numberOfRanges < 3)
            continue;
        NSString* key = [xml substringWithRange:[match rangeAtIndex:1]];
        const int value = [[xml substringWithRange:[match rangeAtIndex:2]] intValue];
        applyParameter(key, value);
    }

    NSRegularExpression* operatorRegex = [NSRegularExpression regularExpressionWithPattern:@"<Operator\\s+([^/>]+)\\s*/>"
                                                                                  options:0
                                                                                    error:&regexError];
    NSRegularExpression* attributeRegex = [NSRegularExpression regularExpressionWithPattern:@"([a-zA-Z0-9]+)=\"(-?\\d+)\""
                                                                                   options:0
                                                                                     error:&regexError];
    for (NSTextCheckingResult* match in [operatorRegex matchesInString:xml options:0 range:NSMakeRange(0, xml.length)])
    {
        if (match.numberOfRanges < 2)
            continue;
        NSString* attributes = [xml substringWithRange:[match rangeAtIndex:1]];
        NSMutableDictionary<NSString*, NSNumber*>* values = [NSMutableDictionary dictionary];
        for (NSTextCheckingResult* attribute in [attributeRegex matchesInString:attributes options:0 range:NSMakeRange(0, attributes.length)])
        {
            if (attribute.numberOfRanges < 3)
                continue;
            NSString* key = [attributes substringWithRange:[attribute rangeAtIndex:1]];
            NSNumber* value = @([[attributes substringWithRange:[attribute rangeAtIndex:2]] intValue]);
            values[key] = value;
        }

        const int opIndex = clamp([values[@"index"] intValue], 0, opaline::kOperatorCount - 1);
        auto& op = patch.operators[static_cast<std::size_t>(opIndex)];
        op.ratioIndex = clamp([values[@"ratio"] intValue], 0, 63);
        op.detune = clamp([values[@"detune"] intValue], -3, 3);
        op.level = clamp([values[@"level"] intValue], 0, 99);
        op.rateScale = clamp([values[@"ratesc"] intValue], 0, 3);
        op.levelScale = clamp([values[@"levelsc"] intValue], 0, 99);
        op.velocity = clamp([values[@"vel"] intValue], 0, 7);
        op.envelope.attackRate = clamp([values[@"ar"] intValue], 0, 31);
        op.envelope.decay1Rate = clamp([values[@"d1r"] intValue], 0, 31);
        op.envelope.decay1Level = clamp([values[@"d1l"] intValue], 0, 15);
        op.envelope.decay2Rate = clamp([values[@"d2r"] intValue], 0, 31);
        op.envelope.releaseRate = clamp([values[@"rr"] intValue], 0, 15);
        op.enabled = [values[@"enabled"] intValue] != 0;
        op.ampModEnable = [values[@"am"] intValue] != 0;
    }

    NSString* voiceName = nil;
    NSRegularExpression* nameRegex = [NSRegularExpression regularExpressionWithPattern:@"<opalineVoice[^>]*name=\"([^\"]*)\""
                                                                               options:0
                                                                                 error:&regexError];
    NSTextCheckingResult* nameMatch = [nameRegex firstMatchInString:xml options:0 range:NSMakeRange(0, xml.length)];
    if (nameMatch != nil && nameMatch.numberOfRanges > 1)
        voiceName = [xml substringWithRange:[nameMatch rangeAtIndex:1]];
    if (voiceName == nil || voiceName.length == 0)
        voiceName = name;

    {
        std::lock_guard<std::mutex> lock(engineMutex);
        [self applyRealtimeCommandsNoLock];
        engine->panic();
        engineB->panic();
        currentPatch = opaline::normalizePatch(patch);
        currentVoiceName = voiceName != nil && voiceName.length > 0 ? std::string(voiceName.UTF8String) : "INIT VOICE";
        effectsEnabled = importedEffectsEnabled;
        [self applyCurrentPatchNoLock];
    }
    return YES;
}

- (BOOL)loadSingleVoiceData:(NSData*)data fileName:(NSString*)fileName
{
    if (data == nil || data.length == 0 || fileName == nil)
        return NO;

    NSString* extension = fileName.pathExtension.lowercaseString;
    NSString* fallbackName = fileName.stringByDeletingPathExtension;
    if ([extension isEqualToString:@"opalinevoice"])
        return [self loadSingleVoiceXMLData:data fallbackName:fallbackName];

    try
    {
        const auto* first = static_cast<const std::uint8_t*>(data.bytes);
        std::vector<std::uint8_t> bytes(first, first + data.length);
        const auto imported = opaline::importChipVoices(bytes,
                                                        std::string(extension.UTF8String),
                                                        std::string(fallbackName.UTF8String));
        if (imported.voices.empty())
            return NO;

        const auto& voice = imported.voices.front();
        std::lock_guard<std::mutex> lock(engineMutex);
        [self applyRealtimeCommandsNoLock];
        engine->panic();
        engineB->panic();
        currentPatch = opaline::normalizePatch(voice.patch);
        currentVoiceName = voice.name;
        effectsEnabled = NO;
        [self applyCurrentPatchNoLock];
        return YES;
    }
    catch (...)
    {
        return NO;
    }
}

- (void)initializeCurrentVoice
{
    std::lock_guard<std::mutex> lock(engineMutex);
    [self applyRealtimeCommandsNoLock];
    engine->panic();
    currentPatch = opaline::normalizePatch(opaline::OpalinePatch {});
    currentVoiceName = "INIT VOICE";
    [self applyCurrentPatchNoLock];
}

- (void)copyCurrentVoice
{
    std::lock_guard<std::mutex> lock(engineMutex);
    [self applyRealtimeCommandsNoLock];
    copiedVoice.patch = opaline::normalizePatch(currentPatch);
    copiedVoice.name = currentVoiceName.substr(0, 10);
    if (copiedVoice.name.empty())
        copiedVoice.name = "INIT VOICE";
    copiedVoice.vmem = opaline::encodeCompatibleVmemVoice(copiedVoice);
    copiedVoice.hasVmem = true;
    hasCopiedPatch = YES;
}

- (BOOL)pasteCopiedVoice
{
    std::lock_guard<std::mutex> lock(engineMutex);
    [self applyRealtimeCommandsNoLock];
    if (!hasCopiedPatch)
        return NO;

    engine->panic();
    currentPatch = opaline::normalizePatch(copiedVoice.patch);
    currentVoiceName = copiedVoice.name.substr(0, 10);
    if (currentVoiceName.empty())
        currentVoiceName = "INIT VOICE";
    [self applyCurrentPatchNoLock];
    return YES;
}

- (BOOL)hasCopiedVoice
{
    std::lock_guard<std::mutex> lock(engineMutex);
    return hasCopiedPatch;
}

- (void)storeCurrentVoice
{
    std::lock_guard<std::mutex> lock(engineMutex);
    [self applyRealtimeCommandsNoLock];
    [self syncCurrentVoiceToLibraryNoLock];
}

- (void)syncCurrentVoiceToLibraryNoLock
{
    const int voiceIndex = std::max(0, std::min(currentVoice, opaline::kOpalineVoiceBankSize - 1));
    auto& voice = opaline::voiceAt(library, currentBank, voiceIndex);
    voice.patch = opaline::normalizePatch(currentPatch);
    voice.effectsEnabled = effectsEnabled;
    voice.name = currentVoiceName.substr(0, 10);
    if (voice.name.empty())
        voice.name = "VOICE " + std::to_string(voiceIndex + 1);
    voice.vmem = opaline::encodeCompatibleVmemVoice(voice);
    voice.hasVmem = true;
    currentVoiceName = voice.name;
}

- (NSDictionary<NSString*, NSNumber*>*)currentPatchSnapshot
{
    opaline::OpalinePatch patch;
    BOOL effectsEnabledSnapshot = NO;
    {
        std::lock_guard<std::mutex> lock(engineMutex);
        [self applyRealtimeCommandsNoLock];
        patch = currentPatch;
        effectsEnabledSnapshot = effectsEnabled;
    }

    NSMutableDictionary<NSString*, NSNumber*>* snapshot = [NSMutableDictionary dictionary];

    snapshot[@"alg"] = @(patch.algorithm);
    snapshot[@"fb"] = @(patch.feedback);
    snapshot[@"pr1"] = @(patch.pitchEnvelope.rate1);
    snapshot[@"pr2"] = @(patch.pitchEnvelope.rate2);
    snapshot[@"pr3"] = @(patch.pitchEnvelope.rate3);
    snapshot[@"pl1"] = @(patch.pitchEnvelope.level1);
    snapshot[@"pl2"] = @(patch.pitchEnvelope.level2);
    snapshot[@"pl3"] = @(patch.pitchEnvelope.level3);
    snapshot[@"lfo.speed"] = @(patch.lfo.speed);
    snapshot[@"lfo.delay"] = @(patch.lfo.delay);
    snapshot[@"lfo.pmd"] = @(patch.lfo.pitchDepth);
    snapshot[@"lfo.amd"] = @(patch.lfo.ampDepth);
    snapshot[@"lfo.pms"] = @(patch.lfo.pitchSensitivity);
    snapshot[@"lfo.ams"] = @(patch.lfo.ampSensitivity);
    snapshot[@"lfo.wave"] = @(patch.lfo.wave);
    snapshot[@"lfo.sync"] = @(patch.lfo.sync ? 1 : 0);
    snapshot[@"fx.reverb"] = @(patch.effects.reverb);
    snapshot[@"fx.delay"] = @(patch.effects.delay);
    snapshot[@"fx.chorus"] = @(patch.effects.chorus);
    snapshot[@"fx.revmix"] = @(patch.effects.mix);
    snapshot[@"fx.dlymix"] = @(patch.effects.echoMix);
    snapshot[@"fx.tone"] = @(patch.effects.tone);
    snapshot[@"fx.enabled"] = @(effectsEnabledSnapshot ? 1 : 0);

    for (int opIndex = 0; opIndex < opaline::kOperatorCount; ++opIndex)
    {
        const auto& op = patch.operators[static_cast<std::size_t>(opIndex)];
        NSString* prefix = [NSString stringWithFormat:@"op%d.", opIndex + 1];
        snapshot[[prefix stringByAppendingString:@"ratio"]] = @(op.ratioIndex);
        snapshot[[prefix stringByAppendingString:@"detune"]] = @(op.detune);
        snapshot[[prefix stringByAppendingString:@"level"]] = @(op.level);
        snapshot[[prefix stringByAppendingString:@"ratesc"]] = @(op.rateScale);
        snapshot[[prefix stringByAppendingString:@"levelsc"]] = @(op.levelScale);
        snapshot[[prefix stringByAppendingString:@"vel"]] = @(op.velocity);
        snapshot[[prefix stringByAppendingString:@"ar"]] = @(op.envelope.attackRate);
        snapshot[[prefix stringByAppendingString:@"d1r"]] = @(op.envelope.decay1Rate);
        snapshot[[prefix stringByAppendingString:@"d1l"]] = @(op.envelope.decay1Level);
        snapshot[[prefix stringByAppendingString:@"d2r"]] = @(op.envelope.decay2Rate);
        snapshot[[prefix stringByAppendingString:@"rr"]] = @(op.envelope.releaseRate);
        snapshot[[prefix stringByAppendingString:@"enabled"]] = @(op.enabled ? 1 : 0);
        snapshot[[prefix stringByAppendingString:@"am"]] = @(op.ampModEnable ? 1 : 0);
    }

    return snapshot;
}

- (double)operatorRatioForIndex:(int)index
{
    const int safeIndex = std::max(0, std::min(index, 63));
    return opaline::opalineRatios()[static_cast<std::size_t>(safeIndex)];
}

- (void)setPatchParameter:(NSString*)parameter value:(int)value
{
    if ([parameter isEqualToString:@"fx.enabled"])
    {
        enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                               { RealtimeCommandType::effectsEnabled, value != 0 ? 1 : 0 });
        return;
    }

    const auto parameterId = patchParameterId(parameter);
    if (parameterId == PatchParameterId::invalid)
        return;
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::patchParameter,
                             static_cast<int>(parameterId), value });
}

- (void)setOperatorParameter:(int)operatorIndex parameter:(NSString*)parameter value:(int)value
{
    const auto parameterId = operatorParameterId(parameter);
    if (parameterId == OperatorParameterId::invalid)
        return;
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::operatorParameter,
                             operatorIndex, static_cast<int>(parameterId), 0.0, value });
}

- (void)setOperatorEnabled:(int)operatorIndex enabled:(BOOL)enabled
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::operatorEnabled,
                             operatorIndex, enabled ? 1 : 0 });
}

- (void)setOperatorAmpModEnabled:(int)operatorIndex enabled:(BOOL)enabled
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::operatorAmpModEnabled,
                             operatorIndex, enabled ? 1 : 0 });
}

- (void)setEffectsEnabled:(BOOL)enabled
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::effectsEnabled, enabled ? 1 : 0 });
}

- (void)setPerformanceMode:(int)mode
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::performanceMode, mode });
}

- (void)setDualDetune:(int)value
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::dualDetune, value });
}

- (void)setSplitPoint:(int)value
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::splitPoint, value });
}

- (void)setABBalance:(int)value
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::abBalance, value });
}

- (void)noteOn:(int)note velocity:(int)velocity
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::noteOn, note, velocity, 0.0 });
}

- (void)noteOff:(int)note
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::noteOff, note, 0, 0.0 });
}

- (void)setPitchBend:(double)value
{
    const double normalized = std::max(-1.0, std::min(value, 1.0));
    currentPitchBend.store(normalized, std::memory_order_release);
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::pitchBend, 0, 0, normalized });
}

- (void)setPitchBendRange:(int)value
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::pitchBendRange, value });
}

- (void)setPortamento:(int)value
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::portamento, value });
}

- (void)setPortamentoModeA:(int)mode
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::portamentoModeA, mode });
}

- (void)setPortamentoModeB:(int)mode
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::portamentoModeB, mode });
}

- (void)setPortamentoFootSwitch:(BOOL)down
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::portamentoFootSwitch, down ? 1 : 0, 0, 0.0 });
}

- (void)setSustainPedal:(BOOL)down
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::sustainPedal, down ? 1 : 0, 0, 0.0 });
}

- (void)setModWheelPitchRange:(int)value ampRange:(int)ampRange
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::modWheelRanges, value, ampRange });
}

- (void)setModWheel:(double)value
{
    const double normalized = std::max(0.0, std::min(value, 1.0));
    currentModWheel.store(normalized, std::memory_order_release);
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::modWheel, 0, 0, normalized });
}

- (void)setMonoMode:(BOOL)enabled
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::monoModeA, enabled ? 1 : 0 });
}

- (void)setMonoModeB:(BOOL)enabled
{
    enqueueRealtimeCommand(realtimeCommands, realtimeCommandOverflowed,
                           { RealtimeCommandType::monoModeB, enabled ? 1 : 0 });
}

- (void)applyRealtimeCommandsNoLock
{
    const auto panicAndResetScope = [&]
    {
        engine->panic();
        engineB->panic();
        scopeHeldNoteCounts.fill(0);
        scopeHeldNoteTotal = 0;
        scopeTriggerNote.store(-1, std::memory_order_release);
    };

    RealtimeCommand command;
    if (realtimeCommandOverflowed.exchange(false, std::memory_order_acq_rel))
    {
        while (realtimeCommands.pop(command)) {}
        panicAndResetScope();
        return;
    }

    bool patchChanged = false;
    const auto clamp = [](const int value, const int low, const int high)
    {
        return std::max(low, std::min(value, high));
    };

    while (realtimeCommands.pop(command))
    {
        switch (command.type)
        {
            case RealtimeCommandType::noteOn:
            {
                const int note = command.value1;
                const int velocity = command.value2;
                const int safeNote = std::max(0, std::min(note, 127));
                ++scopeHeldNoteCounts[static_cast<std::size_t>(safeNote)];
                if (scopeHeldNoteTotal == 0 || safeNote >= scopeTriggerNote.load(std::memory_order_relaxed))
                    scopeTriggerNote.store(safeNote, std::memory_order_release);
                ++scopeHeldNoteTotal;

                switch (performanceMode)
                {
                    case 1:
                        engine->noteOn(note, velocity);
                        engineB->noteOn(note, velocity);
                        break;
                    case 2:
                        if (note <= splitPoint)
                            engine->noteOn(note, velocity);
                        else
                            engineB->noteOn(note, velocity);
                        break;
                    default:
                        engine->noteOn(note, velocity);
                        break;
                }
                break;
            }

            case RealtimeCommandType::noteOff:
            {
                const int note = command.value1;
                const int safeNote = std::max(0, std::min(note, 127));
                auto& heldCount = scopeHeldNoteCounts[static_cast<std::size_t>(safeNote)];
                if (heldCount > 0)
                {
                    --heldCount;
                    scopeHeldNoteTotal = std::max(0, scopeHeldNoteTotal - 1);
                }
                if (scopeHeldNoteTotal > 0)
                {
                    for (int triggerNote = 127; triggerNote >= 0; --triggerNote)
                    {
                        if (scopeHeldNoteCounts[static_cast<std::size_t>(triggerNote)] > 0)
                        {
                            scopeTriggerNote.store(triggerNote, std::memory_order_release);
                            break;
                        }
                    }
                }
                engine->noteOff(note);
                engineB->noteOff(note);
                break;
            }

            case RealtimeCommandType::pitchBend:
            {
                engine->setPitchBend(command.normalizedValue);
                const double detunedB = std::max(-1.0, std::min(1.0,
                    command.normalizedValue + static_cast<double>(dualDetune) / 64.0));
                engineB->setPitchBend(detunedB);
                break;
            }

            case RealtimeCommandType::modWheel:
                engine->setModWheel(command.normalizedValue);
                engineB->setModWheel(command.normalizedValue);
                break;

            case RealtimeCommandType::sustainPedal:
                engine->setSustainPedal(command.value1 != 0);
                engineB->setSustainPedal(command.value1 != 0);
                break;

            case RealtimeCommandType::portamentoFootSwitch:
                engine->setPortamentoFootSwitch(command.value1 != 0);
                engineB->setPortamentoFootSwitch(command.value1 != 0);
                break;

            case RealtimeCommandType::pitchBendRange:
                pitchBendRange = clamp(command.value1, 0, 12);
                engine->setPitchBendRange(pitchBendRange);
                engineB->setPitchBendRange(pitchBendRange);
                break;

            case RealtimeCommandType::portamento:
                portamento = clamp(command.value1, 0, 99);
                engine->setPortamento(portamento);
                engineB->setPortamento(portamento);
                break;

            case RealtimeCommandType::portamentoModeA:
                portamentoModeA = clamp(command.value1, 0, 2);
                engine->panic();
                engine->setPortamentoMode(portamentoModeA);
                break;

            case RealtimeCommandType::portamentoModeB:
                portamentoModeB = clamp(command.value1, 0, 2);
                engineB->panic();
                engineB->setPortamentoMode(portamentoModeB);
                break;

            case RealtimeCommandType::modWheelRanges:
                modWheelPitchRange = clamp(command.value1, 0, 99);
                modWheelAmpRange = clamp(command.value2, 0, 99);
                engine->setModWheelRanges(modWheelPitchRange, modWheelAmpRange);
                engineB->setModWheelRanges(modWheelPitchRange, modWheelAmpRange);
                break;

            case RealtimeCommandType::monoModeA:
                monoA = command.value1 != 0;
                if (!monoA && portamentoModeA == 2)
                    portamentoModeA = 1;
                engine->panic();
                engine->setMonoMode(monoA);
                engine->setPortamentoMode(portamentoModeA);
                break;

            case RealtimeCommandType::monoModeB:
                monoB = command.value1 != 0;
                if (!monoB && portamentoModeB == 2)
                    portamentoModeB = 1;
                engineB->panic();
                engineB->setMonoMode(monoB);
                engineB->setPortamentoMode(portamentoModeB);
                break;

            case RealtimeCommandType::dualDetune:
                dualDetune = clamp(command.value1, -16, 16);
                [self applyPitchBendNoLock];
                break;

            case RealtimeCommandType::splitPoint:
                splitPoint = clamp(command.value1, 0, 127);
                break;

            case RealtimeCommandType::abBalance:
                abBalance = clamp(command.value1, -100, 100);
                break;

            case RealtimeCommandType::performanceMode:
                performanceMode = clamp(command.value1, 0, 2);
                engine->panic();
                engineB->panic();
                engine->setVoiceLimit(performanceMode == 0 ? 8 : 4);
                engineB->setVoiceLimit(4);
                scopeHeldNoteCounts.fill(0);
                scopeHeldNoteTotal = 0;
                scopeTriggerNote.store(-1, std::memory_order_release);
                break;

            case RealtimeCommandType::patchParameter:
                switch (static_cast<PatchParameterId>(command.value1))
                {
                    case PatchParameterId::algorithm: currentPatch.algorithm = clamp(command.value2, 1, 8); break;
                    case PatchParameterId::feedback: currentPatch.feedback = clamp(command.value2, 0, 7); break;
                    case PatchParameterId::pitchRate1: currentPatch.pitchEnvelope.rate1 = clamp(command.value2, 0, 99); break;
                    case PatchParameterId::pitchRate2: currentPatch.pitchEnvelope.rate2 = clamp(command.value2, 0, 99); break;
                    case PatchParameterId::pitchRate3: currentPatch.pitchEnvelope.rate3 = clamp(command.value2, 0, 99); break;
                    case PatchParameterId::pitchLevel1: currentPatch.pitchEnvelope.level1 = clamp(command.value2, 0, 99); break;
                    case PatchParameterId::pitchLevel2: currentPatch.pitchEnvelope.level2 = clamp(command.value2, 0, 99); break;
                    case PatchParameterId::pitchLevel3: currentPatch.pitchEnvelope.level3 = clamp(command.value2, 0, 99); break;
                    case PatchParameterId::lfoSpeed: currentPatch.lfo.speed = clamp(command.value2, 0, 99); break;
                    case PatchParameterId::lfoDelay: currentPatch.lfo.delay = clamp(command.value2, 0, 99); break;
                    case PatchParameterId::lfoPitchDepth: currentPatch.lfo.pitchDepth = clamp(command.value2, 0, 99); break;
                    case PatchParameterId::lfoAmpDepth: currentPatch.lfo.ampDepth = clamp(command.value2, 0, 99); break;
                    case PatchParameterId::lfoPitchSensitivity: currentPatch.lfo.pitchSensitivity = clamp(command.value2, 0, 7); break;
                    case PatchParameterId::lfoAmpSensitivity: currentPatch.lfo.ampSensitivity = clamp(command.value2, 0, 3); break;
                    case PatchParameterId::lfoWave: currentPatch.lfo.wave = clamp(command.value2, 0, 3); break;
                    case PatchParameterId::lfoSync: currentPatch.lfo.sync = command.value2 != 0; break;
                    case PatchParameterId::reverb: currentPatch.effects.reverb = clamp(command.value2, 0, 99); break;
                    case PatchParameterId::delay: currentPatch.effects.delay = clamp(command.value2, 0, 99); break;
                    case PatchParameterId::chorus: currentPatch.effects.chorus = clamp(command.value2, 0, 99); break;
                    case PatchParameterId::reverbMix: currentPatch.effects.mix = clamp(command.value2, 0, 99); break;
                    case PatchParameterId::delayMix: currentPatch.effects.echoMix = clamp(command.value2, 0, 99); break;
                    case PatchParameterId::tone: currentPatch.effects.tone = clamp(command.value2, 0, 99); break;
                    case PatchParameterId::invalid: break;
                }
                patchChanged = true;
                break;

            case RealtimeCommandType::operatorParameter:
            {
                const int opIndex = clamp(command.value1, 0, opaline::kOperatorCount - 1);
                auto& op = currentPatch.operators[static_cast<std::size_t>(opIndex)];
                switch (static_cast<OperatorParameterId>(command.value2))
                {
                    case OperatorParameterId::ratio: op.ratioIndex = clamp(command.value3, 0, 63); break;
                    case OperatorParameterId::detune: op.detune = clamp(command.value3, -3, 3); break;
                    case OperatorParameterId::level: op.level = clamp(command.value3, 0, 99); break;
                    case OperatorParameterId::rateScale: op.rateScale = clamp(command.value3, 0, 3); break;
                    case OperatorParameterId::levelScale: op.levelScale = clamp(command.value3, 0, 99); break;
                    case OperatorParameterId::velocity: op.velocity = clamp(command.value3, 0, 7); break;
                    case OperatorParameterId::attackRate: op.envelope.attackRate = clamp(command.value3, 0, 31); break;
                    case OperatorParameterId::decay1Rate: op.envelope.decay1Rate = clamp(command.value3, 0, 31); break;
                    case OperatorParameterId::decay1Level: op.envelope.decay1Level = clamp(command.value3, 0, 15); break;
                    case OperatorParameterId::decay2Rate: op.envelope.decay2Rate = clamp(command.value3, 0, 31); break;
                    case OperatorParameterId::releaseRate: op.envelope.releaseRate = clamp(command.value3, 0, 15); break;
                    case OperatorParameterId::invalid: break;
                }
                patchChanged = true;
                break;
            }

            case RealtimeCommandType::operatorEnabled:
            {
                const int opIndex = clamp(command.value1, 0, opaline::kOperatorCount - 1);
                currentPatch.operators[static_cast<std::size_t>(opIndex)].enabled = command.value2 != 0;
                patchChanged = true;
                break;
            }

            case RealtimeCommandType::operatorAmpModEnabled:
            {
                const int opIndex = clamp(command.value1, 0, opaline::kOperatorCount - 1);
                currentPatch.operators[static_cast<std::size_t>(opIndex)].ampModEnable = command.value2 != 0;
                patchChanged = true;
                break;
            }

            case RealtimeCommandType::effectsEnabled:
                effectsEnabled = command.value1 != 0;
                patchChanged = true;
                break;
        }
    }

    if (patchChanged)
        [self applyCurrentPatchNoLock];

    if (realtimeCommandOverflowed.exchange(false, std::memory_order_acq_rel))
    {
        while (realtimeCommands.pop(command)) {}
        panicAndResetScope();
    }
}

- (void)renderLeft:(float*)left right:(float*)right frames:(int)frames
{
    if (left == nullptr || right == nullptr || frames <= 0)
        return;

    std::lock_guard<std::mutex> lock(engineMutex);
    [self applyRealtimeCommandsNoLock];
    for (int offset = 0; offset < frames; offset += kPreparedScratchFrames)
    {
        const int chunkFrames = std::min(frames - offset, kPreparedScratchFrames);
        auto* chunkLeft = left + offset;
        auto* chunkRight = right + offset;
        engine->renderBlock(chunkLeft, chunkRight, chunkFrames);

        if (performanceMode != 0)
        {
            engineB->renderBlock(scratchBLeft.data(), scratchBRight.data(), chunkFrames);
            [self mixEngineBNoLockLeft:chunkLeft right:chunkRight frames:chunkFrames];
        }
        [self pushScopeSamplesNoLock:chunkLeft frames:chunkFrames];
    }
}

- (void)renderToAudioBufferList:(AudioBufferList*)audioBufferList frames:(int)frames
{
    if (audioBufferList == nullptr || frames <= 0)
        return;

    const auto bufferCount = audioBufferList->mNumberBuffers;
    std::lock_guard<std::mutex> lock(engineMutex);
    [self applyRealtimeCommandsNoLock];
    for (int offset = 0; offset < frames; offset += kPreparedScratchFrames)
    {
        const int chunkFrames = std::min(frames - offset, kPreparedScratchFrames);
        engine->renderBlock(scratchLeft.data(), scratchRight.data(), chunkFrames);
        if (performanceMode != 0)
        {
            engineB->renderBlock(scratchBLeft.data(), scratchBRight.data(), chunkFrames);
            [self mixEngineBNoLockLeft:scratchLeft.data() right:scratchRight.data() frames:chunkFrames];
        }
        [self pushScopeSamplesNoLock:scratchLeft.data() frames:chunkFrames];

        if (bufferCount == 1)
        {
            auto& buffer = audioBufferList->mBuffers[0];
            auto* samples = static_cast<float*>(buffer.mData);
            const auto channelCount = std::max<UInt32>(1, buffer.mNumberChannels);
            for (int frame = 0; frame < chunkFrames; ++frame)
            {
                const auto outputFrame = static_cast<std::size_t>(offset + frame);
                samples[outputFrame * channelCount] = scratchLeft[static_cast<std::size_t>(frame)];
                if (channelCount > 1)
                    samples[outputFrame * channelCount + 1] = scratchRight[static_cast<std::size_t>(frame)];
            }
        }
        else if (bufferCount >= 2)
        {
            auto* left = static_cast<float*>(audioBufferList->mBuffers[0].mData) + offset;
            auto* right = static_cast<float*>(audioBufferList->mBuffers[1].mData) + offset;
            std::copy_n(scratchLeft.data(), chunkFrames, left);
            std::copy_n(scratchRight.data(), chunkFrames, right);
        }
    }

}

- (NSData*)scopeSnapshotData
{
    constexpr int historySize = 4096;
    constexpr int displaySize = 128;

    std::array<float, historySize> history {};
    const int newest = scopeWriteIndex.load(std::memory_order_acquire);
    float average = 0.0f;
    float peak = 0.0f;
    for (int i = 0; i < historySize; ++i)
    {
        const int readIndex = (newest + i) & (historySize - 1);
        const float value = scopeSamples[static_cast<std::size_t>(readIndex)].load(std::memory_order_relaxed);
        history[static_cast<std::size_t>(i)] = value;
        average += value;
        peak = std::max(peak, std::abs(value));
    }
    average /= static_cast<float>(historySize);
    for (auto& value : history)
        value -= average;

    const int note = scopeTriggerNote.load(std::memory_order_acquire);
    const double sampleRate = scopeSampleRate.load(std::memory_order_acquire);
    const double frequency = note >= 0
        ? 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0)
        : 0.0;
    const double periodSamples = frequency > 0.0 ? sampleRate / frequency : 256.0;
    const int timeWindowSamples = static_cast<int>(std::round(sampleRate * 0.025));
    const int minimumCycleWindow = static_cast<int>(std::round(periodSamples * 1.5));
    const int viewSamples = std::max(256, std::min(3072,
        std::max(timeWindowSamples, minimumCycleWindow)));

    const int idealCentre = historySize - viewSamples / 2 - 2;
    const int searchRadius = std::max(8, std::min(1536,
        static_cast<int>(std::round(periodSamples))));
    const int slopeSpan = std::max(1, std::min(128,
        static_cast<int>(std::round(periodSamples * 0.125))));
    const int halfView = viewSamples / 2;
    const int minimumCentre = halfView + 1;
    const int maximumCentre = historySize - halfView - 2;
    const int searchStart = std::max(std::max(slopeSpan, minimumCentre),
                                     idealCentre - searchRadius);
    const int searchEnd = std::min(std::min(historySize - slopeSpan - 1, maximumCentre),
                                   idealCentre + searchRadius);
    int centreCrossing = -1;
    int closestDistance = std::numeric_limits<int>::max();
    float closestRise = 0.0f;
    const float minimumRise = peak * 0.025f;
    for (int i = searchStart; i <= searchEnd; ++i)
    {
        if (history[static_cast<std::size_t>(i - 1)] <= 0.0f
            && history[static_cast<std::size_t>(i)] > 0.0f)
        {
            const float rise = history[static_cast<std::size_t>(i + slopeSpan)]
                - history[static_cast<std::size_t>(i - slopeSpan)];
            const int distance = std::abs(i - idealCentre);
            if (rise >= minimumRise
                && (distance < closestDistance || (distance == closestDistance && rise > closestRise)))
            {
                closestDistance = distance;
                closestRise = rise;
                centreCrossing = i;
            }
        }
    }

    const double windowStart = note >= 0 && peak > 1.0e-4f && centreCrossing >= 0
        ? static_cast<double>(centreCrossing) - static_cast<double>(viewSamples) * 0.5
        : static_cast<double>(historySize - viewSamples);
    std::array<float, displaySize> display {};
    float displayPeak = 0.0f;
    for (int point = 0; point < displaySize; ++point)
    {
        const double position = windowStart
            + static_cast<double>(viewSamples) * static_cast<double>(point) / static_cast<double>(displaySize - 1);
        const int index = std::max(0, std::min(historySize - 2, static_cast<int>(position)));
        const float fraction = static_cast<float>(position - static_cast<double>(index));
        const float value = history[static_cast<std::size_t>(index)]
            + (history[static_cast<std::size_t>(index + 1)] - history[static_cast<std::size_t>(index)]) * fraction;
        display[static_cast<std::size_t>(point)] = value;
        displayPeak = std::max(displayPeak, std::abs(value));
    }

    const float displayGain = displayPeak > 1.0e-4f
        ? std::min(24.0f, 1.5f / std::sqrt(displayPeak))
        : 1.0f;
    const bool canStabilize = note >= 0 && centreCrossing >= 0 && displayPeak > 1.0e-4f;
    if (!canStabilize || note != scopeSmoothedNote || !scopeHasSmoothedDisplay)
    {
        for (std::size_t i = 0; i < display.size(); ++i)
            scopeSmoothedDisplay[i] = display[i] * displayGain;
        scopeSmoothedNote = canStabilize ? note : -1;
        scopeHasSmoothedDisplay = canStabilize;
    }
    else
    {
        const float currentFrameWeight = periodSamples > 900.0 ? 0.25f : 0.42f;
        for (std::size_t i = 0; i < display.size(); ++i)
        {
            const float current = display[i] * displayGain;
            scopeSmoothedDisplay[i] += (current - scopeSmoothedDisplay[i]) * currentFrameWeight;
        }
    }

    std::array<float, displaySize> snapshot {};
    for (std::size_t i = 0; i < snapshot.size(); ++i)
        snapshot[i] = std::max(-1.0f, std::min(1.0f, scopeSmoothedDisplay[i]));

    return [NSData dataWithBytes:snapshot.data() length:sizeof(snapshot)];
}

- (void)loadBundledFactoryBank
{
    opaline::OpalineVoiceBank factoryBank;
    if (readBundledFactoryBank(factoryBank))
        library.banks[0] = std::move(factoryBank);
}

- (void)prepareEnginesNoLock
{
    if (scratchLeft.size() < static_cast<std::size_t>(kPreparedScratchFrames))
    {
        scratchLeft.resize(kPreparedScratchFrames);
        scratchRight.resize(kPreparedScratchFrames);
        scratchBLeft.resize(kPreparedScratchFrames);
        scratchBRight.resize(kPreparedScratchFrames);
    }

    engine->prepare(currentSampleRate, 8);
    engineB->prepare(currentSampleRate, 4);
    engine->setVoiceLimit(performanceMode == 0 ? 8 : 4);
    engine->setMonoMode(monoA);
    engineB->setMonoMode(monoB);
    engine->setPortamentoMode(portamentoModeA);
    engineB->setPortamentoMode(portamentoModeB);
    engine->setPitchBendRange(pitchBendRange);
    engineB->setPitchBendRange(pitchBendRange);
    engine->setPortamento(portamento);
    engineB->setPortamento(portamento);
    engine->setModWheelRanges(modWheelPitchRange, modWheelAmpRange);
    engineB->setModWheelRanges(modWheelPitchRange, modWheelAmpRange);
    const double modWheel = currentModWheel.load(std::memory_order_acquire);
    engine->setModWheel(modWheel);
    engineB->setModWheel(modWheel);
    engine->setEffectsEnabled(effectsEnabled);
    engineB->setEffectsEnabled(effectsEnabled);
    [self applyPitchBendNoLock];
}

- (void)applyPatchesNoLock
{
    currentBank = std::max(0, std::min(currentBank, opaline::kOpalineVoiceBankCount - 1));
    currentVoice = std::max(0, std::min(currentVoice, opaline::kOpalineVoiceBankSize - 1));
    currentVoiceB = std::max(0, std::min(currentVoiceB, opaline::kOpalineVoiceBankSize - 1));
    currentPatch = opaline::normalizePatch(opaline::voiceAt(library, currentBank, currentVoice).patch);
    currentPatchB = opaline::normalizePatch(opaline::voiceAt(library, currentBank, currentVoiceB).patch);
    currentVoiceName = opaline::voiceAt(library, currentBank, currentVoice).name;
    currentVoiceBName = opaline::voiceAt(library, currentBank, currentVoiceB).name;
    effectsEnabled = opaline::voiceAt(library, currentBank, currentVoice).effectsEnabled;
    [self applyCurrentPatchNoLock];
}

- (void)applyPitchBendNoLock
{
    const double pitchBend = currentPitchBend.load(std::memory_order_acquire);
    engine->setPitchBend(pitchBend);
    const double detunedB = std::max(-1.0, std::min(1.0, pitchBend + static_cast<double>(dualDetune) / 64.0));
    engineB->setPitchBend(detunedB);
}

- (void)applyCurrentPatchNoLock
{
    currentPatch = opaline::normalizePatch(currentPatch);
    currentPatchB = opaline::normalizePatch(currentPatchB);
    engine->setPatch(currentPatch);
    engineB->setPatch(currentPatchB);
    engine->setEffectsEnabled(effectsEnabled);
    engineB->setEffectsEnabled(effectsEnabled);
}

- (void)mixEngineBNoLockLeft:(float*)left right:(float*)right frames:(int)frames
{
    const float balance = static_cast<float>(std::max(-100, std::min(abBalance, 100))) / 100.0f;
    const float gainA = balance >= 0.0f ? 1.0f : 1.0f + balance;
    const float gainB = balance <= 0.0f ? 1.0f : 1.0f - balance;
    const float mixGain = performanceMode == 1 ? 0.50f : 0.82f;

    for (int frame = 0; frame < frames; ++frame)
    {
        const auto index = static_cast<std::size_t>(frame);
        left[index] = (left[index] * gainA + scratchBLeft[index] * gainB) * mixGain;
        right[index] = (right[index] * gainA + scratchBRight[index] * gainB) * mixGain;
    }
}

- (void)pushScopeSamplesNoLock:(const float*)left frames:(int)frames
{
    if (left == nullptr || frames <= 0)
        return;

    const int scopeStart = scopeWriteIndex.load(std::memory_order_relaxed) & 4095;
    for (int frame = 0; frame < frames; ++frame)
    {
        const int index = (scopeStart + frame) & 4095;
        const float sample = std::max(-1.0f, std::min(1.0f, left[static_cast<std::size_t>(frame)]));
        scopeSamples[static_cast<std::size_t>(index)].store(sample, std::memory_order_relaxed);
    }
    scopeWriteIndex.store((scopeStart + frames) & 4095, std::memory_order_release);
}

@end
