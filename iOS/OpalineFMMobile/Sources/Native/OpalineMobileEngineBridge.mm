#import "OpalineMobileEngineBridge.h"

#include "Engine/OpalineEngine.h"
#include "Engine/OpalineVoiceLibrary.h"

#include <algorithm>
#include <memory>
#include <vector>

@interface OpalineMobileEngineBridge ()
{
    std::unique_ptr<opaline::OpalineEngine> engine;
    opaline::OpalineVoiceLibrary library;
    std::vector<float> scratchLeft;
    std::vector<float> scratchRight;
    int currentBank;
    int currentVoice;
}
@end

@implementation OpalineMobileEngineBridge

- (instancetype)init
{
    self = [super init];
    if (self)
    {
        engine = std::make_unique<opaline::OpalineEngine>();
        library = opaline::makeInitVoiceLibrary();
        currentBank = 0;
        currentVoice = 0;
    }
    return self;
}

- (void)prepareWithSampleRate:(double)sampleRate maxVoices:(int)maxVoices
{
    engine->prepare(sampleRate, maxVoices);
    [self loadBundledFactoryBank];
    [self selectVoiceBank:currentBank voice:currentVoice];
}

- (void)selectVoiceBank:(int)bank voice:(int)voice
{
    currentBank = std::max(0, std::min(bank, opaline::kOpalineVoiceBankCount - 1));
    currentVoice = std::max(0, std::min(voice, opaline::kOpalineVoiceBankSize - 1));
    engine->setPatch(opaline::voiceAt(library, currentBank, currentVoice).patch);
}

- (NSString*)currentVoiceName
{
    const auto& selected = opaline::voiceAt(library, currentBank, currentVoice);
    return [NSString stringWithUTF8String:selected.name.c_str()];
}

- (void)noteOn:(int)note velocity:(int)velocity
{
    engine->noteOn(note, velocity);
}

- (void)noteOff:(int)note
{
    engine->noteOff(note);
}

- (void)setPitchBend:(double)value
{
    engine->setPitchBend(value);
}

- (void)setModWheel:(double)value
{
    engine->setModWheel(value);
}

- (void)setMonoMode:(BOOL)enabled
{
    engine->setMonoMode(enabled);
}

- (void)renderLeft:(float*)left right:(float*)right frames:(int)frames
{
    engine->renderBlock(left, right, frames);
}

- (void)renderToAudioBufferList:(AudioBufferList*)audioBufferList frames:(int)frames
{
    if (audioBufferList == nullptr || frames <= 0)
        return;

    if (scratchLeft.size() < static_cast<std::size_t>(frames))
    {
        scratchLeft.resize(static_cast<std::size_t>(frames));
        scratchRight.resize(static_cast<std::size_t>(frames));
    }

    engine->renderBlock(scratchLeft.data(), scratchRight.data(), frames);

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

@end
