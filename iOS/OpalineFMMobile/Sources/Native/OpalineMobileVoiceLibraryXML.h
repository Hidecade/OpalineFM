#pragma once

#import <Foundation/Foundation.h>

#include "Engine/OpalineVoiceLibrary.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace opaline::mobile
{
namespace detail
{
inline NSData* decodedVoiceData(NSString* encoded)
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

inline NSString* xmlAttribute(NSString* attributes, NSString* name)
{
    NSString* pattern = [NSString stringWithFormat:@"%@=\"([^\"]*)\"", name];
    NSRegularExpression* regex = [NSRegularExpression regularExpressionWithPattern:pattern options:0 error:nil];
    NSTextCheckingResult* match = [regex firstMatchInString:attributes options:0 range:NSMakeRange(0, attributes.length)];
    if (match == nil || match.numberOfRanges < 2)
        return nil;
    return [attributes substringWithRange:[match rangeAtIndex:1]];
}

inline std::string xmlUnescapedString(NSString* text)
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
} // namespace detail

inline bool voiceLibraryFromXMLData(NSData* data, OpalineVoiceLibrary& outputLibrary)
{
    if (data == nil || data.length == 0)
        return false;

    NSString* xml = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if (xml == nil || [xml rangeOfString:@"<compatibleVoiceLibrary"].location == NSNotFound)
        return false;

    try
    {
        OpalineVoiceLibrary restored = makeInitVoiceLibrary();
        NSRegularExpression* bankRegex = [NSRegularExpression regularExpressionWithPattern:@"<Bank\\b([^>]*)>(.*?)</Bank>"
                                                                                   options:NSRegularExpressionDotMatchesLineSeparators
                                                                                     error:nil];
        NSArray<NSTextCheckingResult*>* bankMatches = [bankRegex matchesInString:xml options:0 range:NSMakeRange(0, xml.length)];
        if (bankMatches.count == 0)
            return false;

        for (NSUInteger bankMatchIndex = 0; bankMatchIndex < bankMatches.count; ++bankMatchIndex)
        {
            NSTextCheckingResult* bankMatch = bankMatches[bankMatchIndex];
            NSString* bankAttributes = [xml substringWithRange:[bankMatch rangeAtIndex:1]];
            NSString* bankBody = [xml substringWithRange:[bankMatch rangeAtIndex:2]];
            NSString* bankIndexText = detail::xmlAttribute(bankAttributes, @"index");
            const int bankIndex = std::max(0, std::min(bankIndexText != nil ? bankIndexText.intValue : static_cast<int>(bankMatchIndex),
                                                       kOpalineVoiceBankCount - 1));
            auto& bank = restored.banks[static_cast<std::size_t>(bankIndex)];
            NSString* bankName = detail::xmlAttribute(bankAttributes, @"name");
            if (bankName != nil && bankName.length > 0)
                bank.name = detail::xmlUnescapedString(bankName);

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
                NSString* voiceIndexText = detail::xmlAttribute(voiceAttributes, @"index");
                const int voiceIndex = std::max(0, std::min(voiceIndexText != nil ? voiceIndexText.intValue : static_cast<int>(voiceMatchIndex),
                                                            kOpalineVoiceBankSize - 1));
                NSData* decoded = detail::decodedVoiceData(detail::xmlAttribute(voiceAttributes, @"vmem"));
                if (decoded == nil || decoded.length != static_cast<NSUInteger>(kOpalineVmemVoiceSize))
                    continue;

                std::array<std::uint8_t, kOpalineVmemVoiceSize> vmem {};
                std::memcpy(vmem.data(), decoded.bytes, vmem.size());
                auto voice = decodeCompatibleVmemVoice(vmem);
                NSString* voiceName = detail::xmlAttribute(voiceAttributes, @"name");
                if (voiceName != nil && voiceName.length > 0)
                    voice.name = detail::xmlUnescapedString(voiceName);

                NSRange voiceBodyRange = [voiceMatch rangeAtIndex:2];
                if (voiceBodyRange.location != NSNotFound)
                {
                    NSString* voiceBody = [bankBody substringWithRange:voiceBodyRange];
                    NSRegularExpression* effectsRegex = [NSRegularExpression regularExpressionWithPattern:@"<Effects\\b([^>]*)/>" options:0 error:nil];
                    NSTextCheckingResult* effectsMatch = [effectsRegex firstMatchInString:voiceBody options:0 range:NSMakeRange(0, voiceBody.length)];
                    if (effectsMatch != nil && effectsMatch.numberOfRanges >= 2)
                    {
                        NSString* attributes = [voiceBody substringWithRange:[effectsMatch rangeAtIndex:1]];
                        const auto value = [&](NSString* name, const int fallback)
                        {
                            NSString* text = detail::xmlAttribute(attributes, name);
                            return text != nil ? text.intValue : fallback;
                        };
                        voice.effectsEnabled = value(@"enabled", 1) != 0;
                        voice.patch.effects.reverb = value(@"reverb", 0);
                        voice.patch.effects.mix = value(@"mix", 0);
                        voice.patch.effects.delay = value(@"delay", 0);
                        voice.patch.effects.echoMix = value(@"echoMix", 0);
                        voice.patch.effects.chorus = value(@"chorus", 0);
                        voice.patch.effects.tone = value(@"tone", 50);
                        voice.patch = normalizePatch(voice.patch);
                    }
                }
                bank.voices[static_cast<std::size_t>(voiceIndex)] = voice;
            }
        }

        outputLibrary = std::move(restored);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
} // namespace opaline::mobile
