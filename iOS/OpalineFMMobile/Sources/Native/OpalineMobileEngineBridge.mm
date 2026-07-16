#import "OpalineMobileEngineBridge.h"

#include "Engine/OpalineEngine.h"
#include "Engine/OpalineSysex.h"
#include "Engine/OpalineTables.h"
#include "Engine/OpalineVoiceLibrary.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

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
    std::mutex engineMutex;
    double currentSampleRate;
    double currentPitchBend;
    double currentModWheel;
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

static NSData* decodedVoiceData(NSString* encoded)
{
    if (encoded == nil)
        return nil;

    const std::string text(encoded.UTF8String);
    const auto dot = text.find('.');
    if (dot == std::string::npos)
        return [[NSData alloc] initWithBase64EncodedString:encoded options:0];

    const auto decodedSize = static_cast<std::size_t>(std::max(0, std::atoi(text.substr(0, dot).c_str())));
    std::vector<std::uint8_t> bytes(decodedSize, 0);
    static constexpr char encodingTable[] = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+";
    for (std::size_t character = dot + 1, encodedIndex = 0; character < text.size(); ++character, ++encodedIndex)
    {
        const char* match = std::strchr(encodingTable, text[character]);
        if (match == nullptr)
            continue;
        const int value = static_cast<int>(match - encodingTable);
        for (int bit = 0; bit < 6; ++bit)
        {
            const auto bitPosition = encodedIndex * 6 + static_cast<std::size_t>(bit);
            if (bitPosition >= bytes.size() * 8)
                break;
            bytes[bitPosition / 8] |= ((value >> bit) & 1) << (bitPosition % 8);
        }
    }
    return [NSData dataWithBytes:bytes.data() length:bytes.size()];
}

static NSString* xmlAttribute(NSString* attributes, NSString* name)
{
    NSString* pattern = [NSString stringWithFormat:@"%@=\"([^\"]*)\"", name];
    NSRegularExpression* regex = [NSRegularExpression regularExpressionWithPattern:pattern options:0 error:nil];
    NSTextCheckingResult* match = [regex firstMatchInString:attributes options:0 range:NSMakeRange(0, attributes.length)];
    if (match == nil || match.numberOfRanges < 2)
        return nil;
    return [attributes substringWithRange:[match rangeAtIndex:1]];
}

static std::string xmlUnescapedString(NSString* text)
{
    if (text == nil)
        return {};
    NSString* unescaped = [text stringByReplacingOccurrencesOfString:@"&quot;" withString:@"\""];
    unescaped = [unescaped stringByReplacingOccurrencesOfString:@"&apos;" withString:@"'"];
    unescaped = [unescaped stringByReplacingOccurrencesOfString:@"&gt;" withString:@">"];
    unescaped = [unescaped stringByReplacingOccurrencesOfString:@"&lt;" withString:@"<"];
    unescaped = [unescaped stringByReplacingOccurrencesOfString:@"&amp;" withString:@"&"];
    return std::string(unescaped.UTF8String);
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
        currentPitchBend = 0.0;
        currentModWheel = 0.0;
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
        for (auto& sample : scopeSamples)
            sample.store(0.0f, std::memory_order_relaxed);
    }
    return self;
}

- (void)prepareWithSampleRate:(double)sampleRate maxVoices:(int)maxVoices
{
    std::lock_guard<std::mutex> lock(engineMutex);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
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
    currentVoiceB = std::max(0, std::min(voice, opaline::kOpalineVoiceBankSize - 1));
    currentPatchB = opaline::normalizePatch(opaline::voiceAt(library, currentBank, currentVoiceB).patch);
    currentVoiceBName = opaline::voiceAt(library, currentBank, currentVoiceB).name;
    [self applyCurrentPatchNoLock];
}

- (NSString*)currentVoiceName
{
    std::lock_guard<std::mutex> lock(engineMutex);
    return [NSString stringWithUTF8String:currentVoiceName.c_str()];
}

- (NSString*)currentVoiceBName
{
    std::lock_guard<std::mutex> lock(engineMutex);
    return [NSString stringWithUTF8String:currentVoiceBName.c_str()];
}

- (NSArray<NSString*>*)voiceBankNames
{
    std::lock_guard<std::mutex> lock(engineMutex);
    NSMutableArray<NSString*>* names = [NSMutableArray arrayWithCapacity:opaline::kOpalineVoiceBankCount];
    for (int bank = 0; bank < opaline::kOpalineVoiceBankCount; ++bank)
    {
        const auto& name = library.banks[static_cast<std::size_t>(bank)].name;
        const std::string displayName = name.empty() ? "Bank " + std::to_string(bank + 1) : name;
        [names addObject:[NSString stringWithUTF8String:displayName.c_str()]];
    }
    return names;
}

- (NSArray<NSString*>*)voiceNamesForBank:(int)bank
{
    std::lock_guard<std::mutex> lock(engineMutex);
    const int safeBank = std::max(0, std::min(bank, opaline::kOpalineVoiceBankCount - 1));
    NSMutableArray<NSString*>* names = [NSMutableArray arrayWithCapacity:opaline::kOpalineVoiceBankSize];
    for (int voice = 0; voice < opaline::kOpalineVoiceBankSize; ++voice)
    {
        const auto& selected = opaline::voiceAt(library, safeBank, voice);
        [names addObject:[NSString stringWithUTF8String:selected.name.c_str()]];
    }
    return names;
}

- (BOOL)loadVoiceBankData:(NSData*)data name:(NSString*)name
{
    if (data == nil || data.length == 0)
        return NO;

    std::lock_guard<std::mutex> lock(engineMutex);
    const auto* bytes = static_cast<const std::uint8_t*>(data.bytes);
    std::vector<std::uint8_t> sysex(bytes, bytes + data.length);
    try
    {
        const std::string bankName = name != nil && name.length > 0
            ? std::string(name.UTF8String)
            : "Imported";
        engine->panic();
        engineB->panic();
        library.banks[static_cast<std::size_t>(currentBank)] = opaline::voiceBankFromSysex(sysex, bankName);
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
    std::lock_guard<std::mutex> lock(engineMutex);
    try
    {
        [self syncCurrentVoiceToLibraryNoLock];
        const auto bytes = opaline::voiceBankToSysex(library.banks[static_cast<std::size_t>(currentBank)]);
        return [NSData dataWithBytes:bytes.data() length:bytes.size()];
    }
    catch (...)
    {
        return [NSData data];
    }
}

- (NSData*)voiceLibraryXMLData
{
    std::lock_guard<std::mutex> lock(engineMutex);
    [self syncCurrentVoiceToLibraryNoLock];
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<compatibleVoiceLibrary version=\"2\">\n";
    for (int bankIndex = 0; bankIndex < opaline::kOpalineVoiceBankCount; ++bankIndex)
    {
        const auto& bank = library.banks[static_cast<std::size_t>(bankIndex)];
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
    if (data == nil || data.length == 0)
        return NO;

    NSString* xml = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if (xml == nil || [xml rangeOfString:@"<compatibleVoiceLibrary"].location == NSNotFound)
        return NO;

    std::lock_guard<std::mutex> lock(engineMutex);
    try
    {
        opaline::OpalineVoiceLibrary restored = opaline::makeInitVoiceLibrary();
        NSRegularExpression* bankRegex = [NSRegularExpression regularExpressionWithPattern:@"<Bank\\b([^>]*)>(.*?)</Bank>"
                                                                                   options:NSRegularExpressionDotMatchesLineSeparators
                                                                                     error:nil];
        NSArray<NSTextCheckingResult*>* bankMatches = [bankRegex matchesInString:xml options:0 range:NSMakeRange(0, xml.length)];
        for (NSUInteger bankMatchIndex = 0; bankMatchIndex < bankMatches.count; ++bankMatchIndex)
        {
            NSTextCheckingResult* bankMatch = bankMatches[bankMatchIndex];
            NSString* bankAttributes = [xml substringWithRange:[bankMatch rangeAtIndex:1]];
            NSString* bankBody = [xml substringWithRange:[bankMatch rangeAtIndex:2]];
            NSString* bankIndexText = xmlAttribute(bankAttributes, @"index");
            const int bankIndex = std::max(0, std::min(bankIndexText != nil ? bankIndexText.intValue : static_cast<int>(bankMatchIndex),
                                                       opaline::kOpalineVoiceBankCount - 1));
            auto& bank = restored.banks[static_cast<std::size_t>(bankIndex)];
            NSString* bankName = xmlAttribute(bankAttributes, @"name");
            if (bankName != nil && bankName.length > 0)
                bank.name = xmlUnescapedString(bankName);

            NSRegularExpression* voiceRegex = [NSRegularExpression regularExpressionWithPattern:@"<Voice\\b([^>]*)>(.*?)</Voice>|<Voice\\b([^>]*)/>"
                                                                                       options:NSRegularExpressionDotMatchesLineSeparators
                                                                                         error:nil];
            NSArray<NSTextCheckingResult*>* voiceMatches = [voiceRegex matchesInString:bankBody options:0 range:NSMakeRange(0, bankBody.length)];
            for (NSUInteger voiceMatchIndex = 0; voiceMatchIndex < voiceMatches.count; ++voiceMatchIndex)
            {
                NSTextCheckingResult* voiceMatch = voiceMatches[voiceMatchIndex];
                NSRange attributesRange = [voiceMatch rangeAtIndex:1];
                if (attributesRange.location == NSNotFound)
                    attributesRange = [voiceMatch rangeAtIndex:3];
                NSString* voiceAttributes = [bankBody substringWithRange:attributesRange];
                NSString* voiceIndexText = xmlAttribute(voiceAttributes, @"index");
                const int voiceIndex = std::max(0, std::min(voiceIndexText != nil ? voiceIndexText.intValue : static_cast<int>(voiceMatchIndex),
                                                            opaline::kOpalineVoiceBankSize - 1));
                NSString* encoded = xmlAttribute(voiceAttributes, @"vmem");
                NSData* decoded = decodedVoiceData(encoded);
                if (decoded == nil || decoded.length != static_cast<NSUInteger>(opaline::kOpalineVmemVoiceSize))
                    continue;

                std::array<std::uint8_t, opaline::kOpalineVmemVoiceSize> vmem {};
                std::memcpy(vmem.data(), decoded.bytes, vmem.size());
                auto voice = opaline::decodeCompatibleVmemVoice(vmem);
                NSString* voiceName = xmlAttribute(voiceAttributes, @"name");
                if (voiceName != nil && voiceName.length > 0)
                    voice.name = xmlUnescapedString(voiceName);

                NSRange voiceBodyRange = [voiceMatch rangeAtIndex:2];
                if (voiceBodyRange.location != NSNotFound)
                {
                    NSString* voiceBody = [bankBody substringWithRange:voiceBodyRange];
                    NSRegularExpression* effectsRegex = [NSRegularExpression regularExpressionWithPattern:@"<Effects\\b([^>]*)/>"
                                                                                                  options:0
                                                                                                    error:nil];
                    NSTextCheckingResult* effectsMatch = [effectsRegex firstMatchInString:voiceBody options:0 range:NSMakeRange(0, voiceBody.length)];
                    if (effectsMatch != nil && effectsMatch.numberOfRanges >= 2)
                    {
                        NSString* attributes = [voiceBody substringWithRange:[effectsMatch rangeAtIndex:1]];
                        const auto value = [&](NSString* name, const int fallback)
                        {
                            NSString* text = xmlAttribute(attributes, name);
                            return text != nil ? text.intValue : fallback;
                        };
                        voice.effectsEnabled = value(@"enabled", 1) != 0;
                        voice.patch.effects.reverb = value(@"reverb", 0);
                        voice.patch.effects.mix = value(@"mix", 0);
                        voice.patch.effects.delay = value(@"delay", 0);
                        voice.patch.effects.echoMix = value(@"echoMix", 0);
                        voice.patch.effects.chorus = value(@"chorus", 0);
                        voice.patch.effects.tone = value(@"tone", 50);
                        voice.patch = opaline::normalizePatch(voice.patch);
                    }
                }
                bank.voices[static_cast<std::size_t>(voiceIndex)] = voice;
            }
        }

        engine->panic();
        engineB->panic();
        library = std::move(restored);
        [self applyPatchesNoLock];
        return YES;
    }
    catch (...)
    {
        return NO;
    }
}

- (void)reloadBundledFactoryBank
{
    std::lock_guard<std::mutex> lock(engineMutex);
    [self loadBundledFactoryBank];
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
    std::lock_guard<std::mutex> lock(engineMutex);
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<opalineVoice version=\"1\" name=\"" << xmlEscaped(currentVoiceName) << "\">\n";
    xml << "  <Patch>\n";
    xml << "    <Parameter key=\"alg\" value=\"" << currentPatch.algorithm << "\"/>\n";
    xml << "    <Parameter key=\"fb\" value=\"" << currentPatch.feedback << "\"/>\n";
    xml << "    <Parameter key=\"pr1\" value=\"" << currentPatch.pitchEnvelope.rate1 << "\"/>\n";
    xml << "    <Parameter key=\"pr2\" value=\"" << currentPatch.pitchEnvelope.rate2 << "\"/>\n";
    xml << "    <Parameter key=\"pr3\" value=\"" << currentPatch.pitchEnvelope.rate3 << "\"/>\n";
    xml << "    <Parameter key=\"pl1\" value=\"" << currentPatch.pitchEnvelope.level1 << "\"/>\n";
    xml << "    <Parameter key=\"pl2\" value=\"" << currentPatch.pitchEnvelope.level2 << "\"/>\n";
    xml << "    <Parameter key=\"pl3\" value=\"" << currentPatch.pitchEnvelope.level3 << "\"/>\n";
    xml << "    <Parameter key=\"lfo.speed\" value=\"" << currentPatch.lfo.speed << "\"/>\n";
    xml << "    <Parameter key=\"lfo.delay\" value=\"" << currentPatch.lfo.delay << "\"/>\n";
    xml << "    <Parameter key=\"lfo.pmd\" value=\"" << currentPatch.lfo.pitchDepth << "\"/>\n";
    xml << "    <Parameter key=\"lfo.amd\" value=\"" << currentPatch.lfo.ampDepth << "\"/>\n";
    xml << "    <Parameter key=\"lfo.pms\" value=\"" << currentPatch.lfo.pitchSensitivity << "\"/>\n";
    xml << "    <Parameter key=\"lfo.ams\" value=\"" << currentPatch.lfo.ampSensitivity << "\"/>\n";
    xml << "    <Parameter key=\"lfo.wave\" value=\"" << currentPatch.lfo.wave << "\"/>\n";
    xml << "    <Parameter key=\"lfo.sync\" value=\"" << (currentPatch.lfo.sync ? 1 : 0) << "\"/>\n";
    xml << "    <Parameter key=\"fx.reverb\" value=\"" << currentPatch.effects.reverb << "\"/>\n";
    xml << "    <Parameter key=\"fx.delay\" value=\"" << currentPatch.effects.delay << "\"/>\n";
    xml << "    <Parameter key=\"fx.chorus\" value=\"" << currentPatch.effects.chorus << "\"/>\n";
    xml << "    <Parameter key=\"fx.revmix\" value=\"" << currentPatch.effects.mix << "\"/>\n";
    xml << "    <Parameter key=\"fx.dlymix\" value=\"" << currentPatch.effects.echoMix << "\"/>\n";
    xml << "    <Parameter key=\"fx.tone\" value=\"" << currentPatch.effects.tone << "\"/>\n";
    xml << "    <Parameter key=\"fx.enabled\" value=\"" << (effectsEnabled ? 1 : 0) << "\"/>\n";

    for (int opIndex = 0; opIndex < opaline::kOperatorCount; ++opIndex)
    {
        const auto& op = currentPatch.operators[static_cast<std::size_t>(opIndex)];
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

    std::lock_guard<std::mutex> lock(engineMutex);
    opaline::OpalinePatch patch = currentPatch;

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
            effectsEnabled = value != 0;
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

    engine->panic();
    engineB->panic();
    currentPatch = opaline::normalizePatch(patch);
    currentVoiceName = voiceName != nil && voiceName.length > 0 ? std::string(voiceName.UTF8String) : "INIT VOICE";
    [self applyCurrentPatchNoLock];
    return YES;
}

- (void)initializeCurrentVoice
{
    std::lock_guard<std::mutex> lock(engineMutex);
    engine->panic();
    currentPatch = opaline::normalizePatch(opaline::OpalinePatch {});
    currentVoiceName = "INIT VOICE";
    [self applyCurrentPatchNoLock];
}

- (void)copyCurrentVoice
{
    std::lock_guard<std::mutex> lock(engineMutex);
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
    std::lock_guard<std::mutex> lock(engineMutex);
    NSMutableDictionary<NSString*, NSNumber*>* snapshot = [NSMutableDictionary dictionary];
    const auto& patch = currentPatch;

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
    snapshot[@"fx.enabled"] = @(effectsEnabled ? 1 : 0);

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
    std::lock_guard<std::mutex> lock(engineMutex);
    const auto clamp = [](const int v, const int low, const int high) { return std::max(low, std::min(v, high)); };

    if ([parameter isEqualToString:@"alg"])
        currentPatch.algorithm = clamp(value, 1, 8);
    else if ([parameter isEqualToString:@"fb"])
        currentPatch.feedback = clamp(value, 0, 7);
    else if ([parameter isEqualToString:@"pr1"])
        currentPatch.pitchEnvelope.rate1 = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"pr2"])
        currentPatch.pitchEnvelope.rate2 = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"pr3"])
        currentPatch.pitchEnvelope.rate3 = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"pl1"])
        currentPatch.pitchEnvelope.level1 = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"pl2"])
        currentPatch.pitchEnvelope.level2 = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"pl3"])
        currentPatch.pitchEnvelope.level3 = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"lfo.speed"])
        currentPatch.lfo.speed = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"lfo.delay"])
        currentPatch.lfo.delay = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"lfo.pmd"])
        currentPatch.lfo.pitchDepth = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"lfo.amd"])
        currentPatch.lfo.ampDepth = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"lfo.pms"])
        currentPatch.lfo.pitchSensitivity = clamp(value, 0, 7);
    else if ([parameter isEqualToString:@"lfo.ams"])
        currentPatch.lfo.ampSensitivity = clamp(value, 0, 3);
    else if ([parameter isEqualToString:@"lfo.wave"])
        currentPatch.lfo.wave = clamp(value, 0, 3);
    else if ([parameter isEqualToString:@"lfo.sync"])
        currentPatch.lfo.sync = value != 0;
    else if ([parameter isEqualToString:@"fx.reverb"])
        currentPatch.effects.reverb = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"fx.delay"])
        currentPatch.effects.delay = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"fx.chorus"])
        currentPatch.effects.chorus = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"fx.revmix"])
        currentPatch.effects.mix = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"fx.dlymix"])
        currentPatch.effects.echoMix = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"fx.tone"])
        currentPatch.effects.tone = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"fx.enabled"])
    {
        effectsEnabled = value != 0;
        engine->setEffectsEnabled(effectsEnabled);
        engineB->setEffectsEnabled(effectsEnabled);
    }

    [self applyCurrentPatchNoLock];
}

- (void)setOperatorParameter:(int)operatorIndex parameter:(NSString*)parameter value:(int)value
{
    std::lock_guard<std::mutex> lock(engineMutex);
    const int opIndex = std::max(0, std::min(operatorIndex, opaline::kOperatorCount - 1));
    auto& op = currentPatch.operators[static_cast<std::size_t>(opIndex)];
    const auto clamp = [](const int v, const int low, const int high) { return std::max(low, std::min(v, high)); };

    if ([parameter isEqualToString:@"ratio"])
        op.ratioIndex = clamp(value, 0, 63);
    else if ([parameter isEqualToString:@"detune"])
        op.detune = clamp(value, -3, 3);
    else if ([parameter isEqualToString:@"level"])
        op.level = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"ratesc"])
        op.rateScale = clamp(value, 0, 3);
    else if ([parameter isEqualToString:@"levelsc"])
        op.levelScale = clamp(value, 0, 99);
    else if ([parameter isEqualToString:@"vel"])
        op.velocity = clamp(value, 0, 7);
    else if ([parameter isEqualToString:@"ar"])
        op.envelope.attackRate = clamp(value, 0, 31);
    else if ([parameter isEqualToString:@"d1r"])
        op.envelope.decay1Rate = clamp(value, 0, 31);
    else if ([parameter isEqualToString:@"d1l"])
        op.envelope.decay1Level = clamp(value, 0, 15);
    else if ([parameter isEqualToString:@"d2r"])
        op.envelope.decay2Rate = clamp(value, 0, 31);
    else if ([parameter isEqualToString:@"rr"])
        op.envelope.releaseRate = clamp(value, 0, 15);

    [self applyCurrentPatchNoLock];
}

- (void)setOperatorEnabled:(int)operatorIndex enabled:(BOOL)enabled
{
    std::lock_guard<std::mutex> lock(engineMutex);
    const int opIndex = std::max(0, std::min(operatorIndex, opaline::kOperatorCount - 1));
    currentPatch.operators[static_cast<std::size_t>(opIndex)].enabled = enabled;
    [self applyCurrentPatchNoLock];
}

- (void)setOperatorAmpModEnabled:(int)operatorIndex enabled:(BOOL)enabled
{
    std::lock_guard<std::mutex> lock(engineMutex);
    const int opIndex = std::max(0, std::min(operatorIndex, opaline::kOperatorCount - 1));
    currentPatch.operators[static_cast<std::size_t>(opIndex)].ampModEnable = enabled;
    [self applyCurrentPatchNoLock];
}

- (void)setEffectsEnabled:(BOOL)enabled
{
    std::lock_guard<std::mutex> lock(engineMutex);
    effectsEnabled = enabled;
    engine->setEffectsEnabled(effectsEnabled);
    engineB->setEffectsEnabled(effectsEnabled);
}

- (void)setPerformanceMode:(int)mode
{
    std::lock_guard<std::mutex> lock(engineMutex);
    performanceMode = std::max(0, std::min(mode, 2));
    [self prepareEnginesNoLock];
    [self applyPatchesNoLock];
}

- (void)setDualDetune:(int)value
{
    std::lock_guard<std::mutex> lock(engineMutex);
    dualDetune = std::max(-16, std::min(value, 16));
    [self applyPitchBendNoLock];
}

- (void)setSplitPoint:(int)value
{
    std::lock_guard<std::mutex> lock(engineMutex);
    splitPoint = std::max(0, std::min(value, 127));
}

- (void)setABBalance:(int)value
{
    std::lock_guard<std::mutex> lock(engineMutex);
    abBalance = std::max(-100, std::min(value, 100));
}

- (void)noteOn:(int)note velocity:(int)velocity
{
    std::lock_guard<std::mutex> lock(engineMutex);
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
}

- (void)noteOff:(int)note
{
    std::lock_guard<std::mutex> lock(engineMutex);
    engine->noteOff(note);
    engineB->noteOff(note);
}

- (void)setPitchBend:(double)value
{
    std::lock_guard<std::mutex> lock(engineMutex);
    currentPitchBend = std::max(-1.0, std::min(value, 1.0));
    [self applyPitchBendNoLock];
}

- (void)setPitchBendRange:(int)value
{
    std::lock_guard<std::mutex> lock(engineMutex);
    pitchBendRange = std::max(0, std::min(value, 12));
    engine->setPitchBendRange(pitchBendRange);
    engineB->setPitchBendRange(pitchBendRange);
}

- (void)setPortamento:(int)value
{
    std::lock_guard<std::mutex> lock(engineMutex);
    portamento = std::max(0, std::min(value, 99));
    engine->setPortamento(portamento);
    engineB->setPortamento(portamento);
}

- (void)setPortamentoModeA:(int)mode
{
    std::lock_guard<std::mutex> lock(engineMutex);
    portamentoModeA = std::max(0, std::min(mode, 2));
    engine->panic();
    engine->setPortamentoMode(portamentoModeA);
}

- (void)setPortamentoModeB:(int)mode
{
    std::lock_guard<std::mutex> lock(engineMutex);
    portamentoModeB = std::max(0, std::min(mode, 2));
    engineB->panic();
    engineB->setPortamentoMode(portamentoModeB);
}

- (void)setPortamentoFootSwitch:(BOOL)down
{
    std::lock_guard<std::mutex> lock(engineMutex);
    engine->setPortamentoFootSwitch(down);
    engineB->setPortamentoFootSwitch(down);
}

- (void)setSustainPedal:(BOOL)down
{
    std::lock_guard<std::mutex> lock(engineMutex);
    engine->setSustainPedal(down);
    engineB->setSustainPedal(down);
}

- (void)setModWheelPitchRange:(int)value ampRange:(int)ampRange
{
    std::lock_guard<std::mutex> lock(engineMutex);
    modWheelPitchRange = std::max(0, std::min(value, 99));
    modWheelAmpRange = std::max(0, std::min(ampRange, 99));
    engine->setModWheelRanges(modWheelPitchRange, modWheelAmpRange);
    engineB->setModWheelRanges(modWheelPitchRange, modWheelAmpRange);
}

- (void)setModWheel:(double)value
{
    std::lock_guard<std::mutex> lock(engineMutex);
    currentModWheel = std::max(0.0, std::min(value, 1.0));
    engine->setModWheel(currentModWheel);
    engineB->setModWheel(currentModWheel);
}

- (void)setMonoMode:(BOOL)enabled
{
    std::lock_guard<std::mutex> lock(engineMutex);
    monoA = enabled;
    if (!monoA && portamentoModeA == 2)
        portamentoModeA = 1;
    engine->panic();
    engine->setMonoMode(enabled);
    engine->setPortamentoMode(portamentoModeA);
}

- (void)setMonoModeB:(BOOL)enabled
{
    std::lock_guard<std::mutex> lock(engineMutex);
    monoB = enabled;
    if (!monoB && portamentoModeB == 2)
        portamentoModeB = 1;
    engineB->panic();
    engineB->setMonoMode(enabled);
    engineB->setPortamentoMode(portamentoModeB);
}

- (void)renderLeft:(float*)left right:(float*)right frames:(int)frames
{
    if (left == nullptr || right == nullptr || frames <= 0)
        return;

    std::lock_guard<std::mutex> lock(engineMutex);
    engine->renderBlock(left, right, frames);
    if (performanceMode == 0)
    {
        [self pushScopeSamplesNoLock:left frames:frames];
        return;
    }

    if (scratchBLeft.size() < static_cast<std::size_t>(frames))
    {
        scratchBLeft.resize(static_cast<std::size_t>(frames));
        scratchBRight.resize(static_cast<std::size_t>(frames));
    }

    engineB->renderBlock(scratchBLeft.data(), scratchBRight.data(), frames);
    [self mixEngineBNoLockLeft:left right:right frames:frames];
    [self pushScopeSamplesNoLock:left frames:frames];
}

- (void)renderToAudioBufferList:(AudioBufferList*)audioBufferList frames:(int)frames
{
    if (audioBufferList == nullptr || frames <= 0)
        return;

    if (scratchLeft.size() < static_cast<std::size_t>(frames))
    {
        scratchLeft.resize(static_cast<std::size_t>(frames));
        scratchRight.resize(static_cast<std::size_t>(frames));
        scratchBLeft.resize(static_cast<std::size_t>(frames));
        scratchBRight.resize(static_cast<std::size_t>(frames));
    }

    std::lock_guard<std::mutex> lock(engineMutex);
    engine->renderBlock(scratchLeft.data(), scratchRight.data(), frames);
    if (performanceMode != 0)
    {
        engineB->renderBlock(scratchBLeft.data(), scratchBRight.data(), frames);
        [self mixEngineBNoLockLeft:scratchLeft.data() right:scratchRight.data() frames:frames];
    }
    [self pushScopeSamplesNoLock:scratchLeft.data() frames:frames];

    const auto bufferCount = audioBufferList->mNumberBuffers;
    if (bufferCount == 1)
    {
        auto& buffer = audioBufferList->mBuffers[0];
        auto* samples = static_cast<float*>(buffer.mData);
        const auto channelCount = std::max<UInt32>(1, buffer.mNumberChannels);
        for (int frame = 0; frame < frames; ++frame)
        {
            samples[frame * channelCount] = scratchLeft[static_cast<std::size_t>(frame)];
            if (channelCount > 1)
                samples[frame * channelCount + 1] = scratchRight[static_cast<std::size_t>(frame)];
        }
        return;
    }

    if (bufferCount >= 2)
    {
        auto* left = static_cast<float*>(audioBufferList->mBuffers[0].mData);
        auto* right = static_cast<float*>(audioBufferList->mBuffers[1].mData);
        std::copy_n(scratchLeft.data(), frames, left);
        std::copy_n(scratchRight.data(), frames, right);
    }
}

- (NSArray<NSNumber*>*)scopeSnapshot
{
    constexpr int historySize = 4096;
    constexpr int displaySize = 128;
    constexpr int viewSamples = 1024;

    std::array<float, historySize> history {};
    const int newest = scopeWriteIndex.load(std::memory_order_relaxed);
    float average = 0.0f;
    for (int i = 0; i < historySize; ++i)
    {
        const int readIndex = (newest + i) & (historySize - 1);
        const float value = scopeSamples[static_cast<std::size_t>(readIndex)].load(std::memory_order_relaxed);
        history[static_cast<std::size_t>(i)] = value;
        average += value;
    }
    average /= static_cast<float>(historySize);
    for (auto& value : history)
        value -= average;

    const int windowStart = historySize - viewSamples;
    std::array<float, displaySize> display {};
    float displayPeak = 0.0f;
    for (int point = 0; point < displaySize; ++point)
    {
        const double position = static_cast<double>(windowStart)
            + static_cast<double>(viewSamples - 1) * static_cast<double>(point) / static_cast<double>(displaySize - 1);
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

    NSMutableArray<NSNumber*>* snapshot = [NSMutableArray arrayWithCapacity:displaySize];
    for (const auto value : display)
    {
        const float clipped = std::max(-1.0f, std::min(1.0f, value * displayGain));
        [snapshot addObject:@(clipped)];
    }
    return snapshot;
}

- (void)loadBundledFactoryBank
{
    NSString* path = [[NSBundle mainBundle] pathForResource:@"factory" ofType:@"syx"];
    if (path == nil)
        return;

    NSData* data = [NSData dataWithContentsOfFile:path];
    if (data == nil || data.length == 0)
        return;

    const auto* bytes = static_cast<const std::uint8_t*>(data.bytes);
    std::vector<std::uint8_t> sysex(bytes, bytes + data.length);

    try
    {
        library.banks[0] = opaline::voiceBankFromSysex(sysex, "Factory");
    }
    catch (...)
    {
        library = opaline::makeInitVoiceLibrary();
    }
}

- (void)prepareEnginesNoLock
{
    const int voiceCountA = performanceMode == 0 ? 8 : 4;
    engine->prepare(currentSampleRate, voiceCountA);
    engineB->prepare(currentSampleRate, 4);
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
    engine->setModWheel(currentModWheel);
    engineB->setModWheel(currentModWheel);
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
    engine->setPitchBend(currentPitchBend);
    const double detunedB = std::max(-1.0, std::min(1.0, currentPitchBend + static_cast<double>(dualDetune) / 64.0));
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

    for (int frame = 0; frame < frames; ++frame)
    {
        const int index = scopeWriteIndex.fetch_add(1, std::memory_order_relaxed) & 4095;
        const float sample = std::max(-1.0f, std::min(1.0f, left[static_cast<std::size_t>(frame)]));
        scopeSamples[static_cast<std::size_t>(index)].store(sample, std::memory_order_relaxed);
    }
}

@end
