#import "OpalineAUAudioUnit.h"
#import "../Sources/Native/OpalineMobileVoiceLibraryXML.h"

#include "Engine/OpalineEngine.h"
#include "Engine/OpalineVoiceLibrary.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
enum OpalineAUParameterAddress : AUParameterAddress
{
    ParamVoiceA = 0,
    ParamEffectsEnabled,
    ParamMono,
    ParamPortamentoPreset,
    ParamCount
};

constexpr float kAUGain = 0.65f;

int roundedParameterValue(const std::array<std::atomic<float>, ParamCount>& values, const AUParameterAddress address)
{
    return static_cast<int>(std::lround(values[static_cast<std::size_t>(address)].load(std::memory_order_relaxed)));
}

int portamentoModeForPreset(const int preset)
{
    if (preset >= 4)
        return 2;
    return preset > 0 ? 1 : 0;
}

int portamentoValueForPreset(const int preset)
{
    if (preset <= 0)
        return 0;

    switch ((std::min(preset, 6) - 1) % 3)
    {
        case 0: return 18;
        case 1: return 30;
        default: return 42;
    }
}

} // namespace

@interface OpalineAUAudioUnit ()
@property (nonatomic, strong) AUAudioUnitBus* outputBus;
@property (nonatomic, strong) AUAudioUnitBusArray* outputBusArray;
@property (nonatomic, strong) AUAudioUnitBusArray* inputBusArray;
@end

@implementation OpalineAUAudioUnit
{
    opaline::OpalineEngine engine;
    opaline::OpalineVoiceLibrary voiceLibrary;
    opaline::OpalinePatch currentPatch;
    NSArray<NSString*>* voiceDisplayNames;
    std::vector<float> renderLeft;
    std::vector<float> renderRight;
    std::array<std::atomic<float>, ParamCount> parameterValues;
    std::atomic<bool> parametersDirty;
    AUParameterObserverToken parameterObserverToken;
    double preparedSampleRate;
    AVAudioFrameCount preparedFrameCapacity;
    int currentBankIndex;
    int currentVoiceIndex;
    double currentPitchBend;
    double currentModWheel;
}

- (instancetype)initWithComponentDescription:(AudioComponentDescription)componentDescription
                                      options:(AudioComponentInstantiationOptions)options
                                        error:(NSError**)outError
{
    self = [super initWithComponentDescription:componentDescription options:options error:outError];
    if (self == nil)
        return nil;

    AVAudioFormat* format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:44100.0 channels:2];
    NSError* busError = nil;
    _outputBus = [[AUAudioUnitBus alloc] initWithFormat:format error:&busError];
    if (_outputBus == nil)
    {
        if (outError != nullptr)
            *outError = busError;
        return nil;
    }

    _outputBusArray = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self busType:AUAudioUnitBusTypeOutput busses:@[_outputBus]];
    _inputBusArray = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self busType:AUAudioUnitBusTypeInput busses:@[]];
    self.maximumFramesToRender = 4096;
    for (auto& value : parameterValues)
        value.store(0.0f, std::memory_order_relaxed);
    parametersDirty.store(true, std::memory_order_relaxed);
    voiceLibrary = opaline::makeInitVoiceLibrary();
    currentBankIndex = 0;
    currentVoiceIndex = 0;
    [self loadBundledFactoryBank];
    const auto& voice = opaline::voiceAt(voiceLibrary, currentBankIndex, currentVoiceIndex);
    currentPatch = opaline::normalizePatch(voice.patch);
    voiceDisplayNames = [self makeVoiceDisplayNames];
    currentPitchBend = 0.0;
    currentModWheel = 0.0;
    [self installParameterTree];
    preparedSampleRate = 0.0;
    preparedFrameCapacity = 0;
    return self;
}

- (void)installParameterTree
{
    NSMutableArray<AUParameter*>* parameters = [NSMutableArray array];
    [self addParameterTo:parameters identifier:@"voiceA" name:@"Voice A" address:ParamVoiceA min:0 max:31 unit:kAudioUnitParameterUnit_Indexed defaultValue:currentVoiceIndex valueStrings:voiceDisplayNames];
    const AUValue initialEffectsEnabled = opaline::voiceAt(voiceLibrary, currentBankIndex, currentVoiceIndex).effectsEnabled ? 1.0f : 0.0f;
    [self addParameterTo:parameters identifier:@"effectsEnabled" name:@"Effects Enabled" address:ParamEffectsEnabled min:0 max:1 unit:kAudioUnitParameterUnit_Boolean defaultValue:initialEffectsEnabled valueStrings:nil];
    [self addParameterTo:parameters identifier:@"mono" name:@"Mono" address:ParamMono min:0 max:1 unit:kAudioUnitParameterUnit_Boolean defaultValue:0 valueStrings:nil];
    [self addParameterTo:parameters identifier:@"portamentoPreset" name:@"Portamento" address:ParamPortamentoPreset min:0 max:6 unit:kAudioUnitParameterUnit_Indexed defaultValue:0 valueStrings:@[@"Off", @"Full Short", @"Full Medium", @"Full Long", @"Fingered Short", @"Fingered Medium", @"Fingered Long"]];

    self.parameterTree = [AUParameterTree createTreeWithChildren:parameters];
    __unsafe_unretained OpalineAUAudioUnit* audioUnit = self;
    parameterObserverToken = [self.parameterTree tokenByAddingParameterObserver:^(AUParameterAddress address, AUValue value) {
        if (address < ParamCount)
        {
            audioUnit->parameterValues[static_cast<std::size_t>(address)].store(value, std::memory_order_relaxed);
            audioUnit->parametersDirty.store(true, std::memory_order_release);
        }
    }];
}

- (void)addParameterTo:(NSMutableArray<AUParameter*>*)parameters
            identifier:(NSString*)identifier
                  name:(NSString*)name
               address:(AUParameterAddress)address
                   min:(AUValue)minValue
                   max:(AUValue)maxValue
                  unit:(AudioUnitParameterUnit)unit
          defaultValue:(AUValue)defaultValue
          valueStrings:(NSArray<NSString*>* _Nullable)valueStrings
{
    AUParameter* parameter = [AUParameterTree createParameterWithIdentifier:identifier
                                                                       name:name
                                                                    address:address
                                                                        min:minValue
                                                                        max:maxValue
                                                                       unit:unit
                                                                   unitName:nil
                                                                      flags:kAudioUnitParameterFlag_IsWritable | kAudioUnitParameterFlag_IsReadable
                                                               valueStrings:valueStrings
                                                        dependentParameters:nil];
    parameter.value = defaultValue;
    parameterValues[static_cast<std::size_t>(address)].store(defaultValue, std::memory_order_relaxed);
    [parameters addObject:parameter];
}

- (AUAudioUnitBusArray*)outputBusses
{
    return self.outputBusArray;
}

- (AUAudioUnitBusArray*)inputBusses
{
    return self.inputBusArray;
}

- (NSArray<NSString*>*)makeVoiceDisplayNames
{
    NSMutableArray<NSString*>* names = [NSMutableArray arrayWithCapacity:opaline::kOpalineVoiceBankSize];
    for (int voice = 0; voice < opaline::kOpalineVoiceBankSize; ++voice)
    {
        const auto& selected = opaline::voiceAt(voiceLibrary, currentBankIndex, voice);
        NSMutableString* safeName = [NSMutableString stringWithCapacity:selected.name.size()];
        for (const unsigned char character : selected.name)
            [safeName appendFormat:@"%c", character >= 0x20 && character <= 0x7e ? character : ' '];
        NSString* trimmedName = [safeName stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        if (trimmedName.length == 0)
            trimmedName = @"Voice";
        [names addObject:[NSString stringWithFormat:@"%02d %@", voice + 1, trimmedName]];
    }
    return [names copy];
}

- (BOOL)allocateRenderResourcesAndReturnError:(NSError**)outError
{
    if (![super allocateRenderResourcesAndReturnError:outError])
        return NO;

    const double sampleRate = self.outputBus.format.sampleRate > 0.0 ? self.outputBus.format.sampleRate : 44100.0;
    const AVAudioFrameCount frameCapacity = std::max<AVAudioFrameCount>(1, self.maximumFramesToRender);
    renderLeft.assign(static_cast<std::size_t>(frameCapacity), 0.0f);
    renderRight.assign(static_cast<std::size_t>(frameCapacity), 0.0f);

    engine.prepare(sampleRate, 16);
    [self applyParametersToEngine];
    preparedSampleRate = sampleRate;
    preparedFrameCapacity = frameCapacity;
    return YES;
}

- (void)deallocateRenderResources
{
    engine.panic();
    renderLeft.clear();
    renderRight.clear();
    preparedSampleRate = 0.0;
    preparedFrameCapacity = 0;
    [super deallocateRenderResources];
}

- (AUInternalRenderBlock)internalRenderBlock
{
    __unsafe_unretained OpalineAUAudioUnit* audioUnit = self;
    return ^AUAudioUnitStatus(AudioUnitRenderActionFlags* actionFlags,
                              const AudioTimeStamp* timestamp,
                              AVAudioFrameCount frameCount,
                              NSInteger outputBusNumber,
                              AudioBufferList* outputData,
                              const AURenderEvent* realtimeEventListHead,
                              AURenderPullInputBlock pullInputBlock) {
        (void) actionFlags;
        (void) outputBusNumber;
        (void) pullInputBlock;

        if (outputData == nullptr || frameCount == 0)
            return noErr;

        if (frameCount > audioUnit->preparedFrameCapacity
            || audioUnit->renderLeft.size() < frameCount
            || audioUnit->renderRight.size() < frameCount)
        {
            [audioUnit clearOutput:outputData];
            return kAudioUnitErr_TooManyFramesToProcess;
        }

        [audioUnit applyPendingParametersToEngine];
        const AUEventSampleTime startTime = timestamp != nullptr ? timestamp->mSampleTime : 0;
        AVAudioFrameCount renderedFrames = 0;
        for (const AURenderEvent* event = realtimeEventListHead; event != nullptr; event = event->head.next)
        {
            const AUEventSampleTime eventTime = event->head.eventSampleTime;
            const AVAudioFrameCount eventFrame = static_cast<AVAudioFrameCount>(
                std::min<AUEventSampleTime>(
                    std::max<AUEventSampleTime>(0, eventTime - startTime),
                    static_cast<AUEventSampleTime>(frameCount)));
            if (eventFrame > renderedFrames)
            {
                [audioUnit renderFrames:eventFrame - renderedFrames atOffset:renderedFrames];
                renderedFrames = eventFrame;
            }

            [audioUnit handleRenderEvent:event];
        }

        if (renderedFrames < frameCount)
            [audioUnit renderFrames:frameCount - renderedFrames atOffset:renderedFrames];
        [audioUnit copyLeft:audioUnit->renderLeft.data() right:audioUnit->renderRight.data() frames:frameCount toOutput:outputData];
        return noErr;
    };
}

- (NSDictionary<NSString*, id>*)fullState
{
    NSMutableDictionary<NSString*, id>* state = [NSMutableDictionary dictionaryWithCapacity:ParamCount + 4];
    state[@"version"] = @1;
    const int voiceIndex = std::max(0, std::min(roundedParameterValue(parameterValues, ParamVoiceA), opaline::kOpalineVoiceBankSize - 1));
    state[@"voiceName"] = voiceDisplayNames[static_cast<NSUInteger>(voiceIndex)];
    state[@"bankIndex"] = @(currentBankIndex);
    for (AUParameter* parameter in self.parameterTree.allParameters)
    {
        if (parameter.address < ParamCount)
            state[parameter.identifier] = @(parameterValues[static_cast<std::size_t>(parameter.address)].load(std::memory_order_relaxed));
        else
            state[parameter.identifier] = @(parameter.value);
    }
    return state;
}

- (NSDictionary<NSString*, id>*)fullStateForDocument
{
    return [self fullState];
}

- (void)setFullStateForDocument:(NSDictionary<NSString*, id>*)fullStateForDocument
{
    [self setFullState:fullStateForDocument];
}

- (void)setFullState:(NSDictionary<NSString*, id>*)fullState
{
    if (![fullState isKindOfClass:NSDictionary.class])
        return;

    NSNumber* bankIndexValue = fullState[@"bankIndex"];
    if ([bankIndexValue respondsToSelector:@selector(intValue)])
        currentBankIndex = std::max(0, std::min(bankIndexValue.intValue, opaline::kOpalineVoiceBankCount - 1));

    NSNumber* legacyVoiceIndexValue = fullState[@"voiceIndex"];
    if ([legacyVoiceIndexValue respondsToSelector:@selector(intValue)])
        parameterValues[ParamVoiceA].store(std::max(0, std::min(legacyVoiceIndexValue.intValue, opaline::kOpalineVoiceBankSize - 1)), std::memory_order_relaxed);

    for (AUParameter* parameter in self.parameterTree.allParameters)
    {
        NSNumber* value = fullState[parameter.identifier];
        if (![value respondsToSelector:@selector(floatValue)])
            continue;

        parameter.value = std::max(parameter.minValue, std::min(parameter.maxValue, value.floatValue));
        if (parameter.address < ParamCount)
            parameterValues[static_cast<std::size_t>(parameter.address)].store(parameter.value, std::memory_order_relaxed);
    }
    parametersDirty.store(true, std::memory_order_release);
}

- (void)loadBundledFactoryBank
{
    NSURL* libraryURL = [NSBundle.mainBundle URLForResource:@"factory.opalinelibrary" withExtension:@"xml"];
    NSData* libraryData = libraryURL != nil ? [NSData dataWithContentsOfURL:libraryURL] : nil;
    opaline::OpalineVoiceLibrary factoryLibrary;
    if (opaline::mobile::voiceLibraryFromXMLData(libraryData, factoryLibrary))
    {
        voiceLibrary.banks[0] = std::move(factoryLibrary.banks[0]);
        return;
    }

    NSURL* factoryURL = [NSBundle.mainBundle URLForResource:@"factory" withExtension:@"syx"];
    if (factoryURL == nil)
        return;

    NSData* data = [NSData dataWithContentsOfURL:factoryURL];
    if (data == nil || data.length == 0)
        return;

    const auto* bytes = static_cast<const std::uint8_t*>(data.bytes);
    std::vector<std::uint8_t> sysex(bytes, bytes + data.length);
    try
    {
        voiceLibrary.banks[0] = opaline::voiceBankFromSysex(sysex, "Factory");
    }
    catch (...)
    {
        voiceLibrary.banks[0] = opaline::makeInitVoiceBank("Factory");
    }
}

- (void)applyPendingParametersToEngine
{
    if (!parametersDirty.exchange(false, std::memory_order_acq_rel))
        return;

    [self applyParametersToEngine];
}

- (void)applyParametersToEngine
{
    const int requestedVoiceA = roundedParameterValue(parameterValues, ParamVoiceA);
    if (requestedVoiceA != currentVoiceIndex)
    {
        currentVoiceIndex = std::max(0, std::min(requestedVoiceA, opaline::kOpalineVoiceBankSize - 1));
        const auto& voice = opaline::voiceAt(voiceLibrary, currentBankIndex, currentVoiceIndex);
        currentPatch = opaline::normalizePatch(voice.patch);
    }

    engine.setPatch(currentPatch);
    const bool effectsEnabled = roundedParameterValue(parameterValues, ParamEffectsEnabled) != 0;
    const int portamentoPreset = roundedParameterValue(parameterValues, ParamPortamentoPreset);
    engine.setEffectsEnabled(effectsEnabled);
    engine.setMonoMode(roundedParameterValue(parameterValues, ParamMono) != 0);
    engine.setPortamentoMode(portamentoModeForPreset(portamentoPreset));
    engine.setPortamento(portamentoValueForPreset(portamentoPreset));
    engine.setModWheelRanges(99, 0);
    engine.setPitchBend(currentPitchBend);
    engine.setModWheel(currentModWheel);
}

- (void)renderFrames:(AVAudioFrameCount)frames atOffset:(AVAudioFrameCount)offset
{
    if (frames == 0)
        return;

    float* left = renderLeft.data() + offset;
    float* right = renderRight.data() + offset;
    engine.renderBlock(left, right, static_cast<int>(frames));
}

- (void)handleRenderEvent:(const AURenderEvent*)event
{
    if (event == nullptr)
        return;

    switch (event->head.eventType)
    {
        case AURenderEventMIDI:
            [self handleMIDIData:event->MIDI.data length:event->MIDI.length];
            break;
        case AURenderEventMIDISysEx:
            break;
        case AURenderEventParameter:
        case AURenderEventParameterRamp:
            [self handleParameterEvent:event->parameter.parameterAddress value:event->parameter.value];
            break;
        default:
            break;
    }
}

- (void)handleParameterEvent:(AUParameterAddress)address value:(AUValue)value
{
    if (address >= ParamCount)
        return;

    parameterValues[static_cast<std::size_t>(address)].store(value, std::memory_order_relaxed);
    parametersDirty.store(true, std::memory_order_release);
    [self applyPendingParametersToEngine];
}

- (void)handleMIDIData:(const uint8_t*)data length:(NSInteger)length
{
    if (data == nullptr || length < 1)
        return;

    const uint8_t status = data[0] & 0xf0u;
    const int data1 = length > 1 ? data[1] : 0;
    const int data2 = length > 2 ? data[2] : 0;

    switch (status)
    {
        case 0x80:
            [self noteOff:data1];
            break;
        case 0x90:
            if (data2 == 0)
                [self noteOff:data1];
            else
                [self noteOn:data1 velocity:data2];
            break;
        case 0xb0:
            [self handleControlChange:data1 value:data2];
            break;
        case 0xe0:
        {
            const int value14 = data1 | (data2 << 7);
            const double bend = (static_cast<double>(value14) - 8192.0) / 8192.0;
            currentPitchBend = std::max(-1.0, std::min(1.0, bend));
            engine.setPitchBend(currentPitchBend);
            break;
        }
        default:
            break;
    }
}

- (void)noteOn:(int)note velocity:(int)velocity
{
    engine.noteOn(note, velocity);
}

- (void)noteOff:(int)note
{
    engine.noteOff(note);
}

- (void)handleControlChange:(int)controller value:(int)value
{
    switch (controller)
    {
        case 1:
            currentModWheel = std::max(0.0, std::min(1.0, static_cast<double>(value) / 127.0));
            engine.setModWheel(currentModWheel);
            break;
        case 64:
            engine.setSustainPedal(value >= 64);
            break;
        case 65:
            engine.setPortamentoFootSwitch(value >= 64);
            break;
        case 120:
        case 123:
            engine.panic();
            break;
        default:
            break;
    }
}

- (void)clearOutput:(AudioBufferList*)outputData
{
    for (UInt32 bufferIndex = 0; bufferIndex < outputData->mNumberBuffers; ++bufferIndex)
    {
        AudioBuffer& buffer = outputData->mBuffers[bufferIndex];
        if (buffer.mData != nullptr)
            std::fill_n(static_cast<std::uint8_t*>(buffer.mData), buffer.mDataByteSize, 0);
    }
}

- (void)copyLeft:(const float*)left right:(const float*)right frames:(AVAudioFrameCount)frames toOutput:(AudioBufferList*)outputData
{
    if (outputData->mNumberBuffers == 1)
    {
        AudioBuffer& buffer = outputData->mBuffers[0];
        float* samples = static_cast<float*>(buffer.mData);
        const UInt32 channels = std::max<UInt32>(1, buffer.mNumberChannels);
        if (samples == nullptr)
            return;

        for (AVAudioFrameCount frame = 0; frame < frames; ++frame)
        {
            samples[frame * channels] = left[frame] * kAUGain;
            if (channels > 1)
                samples[frame * channels + 1] = right[frame] * kAUGain;
        }
        return;
    }

    if (outputData->mNumberBuffers >= 2)
    {
        float* leftOut = static_cast<float*>(outputData->mBuffers[0].mData);
        float* rightOut = static_cast<float*>(outputData->mBuffers[1].mData);
        if (leftOut != nullptr)
        {
            for (AVAudioFrameCount frame = 0; frame < frames; ++frame)
                leftOut[frame] = left[frame] * kAUGain;
        }
        if (rightOut != nullptr)
        {
            for (AVAudioFrameCount frame = 0; frame < frames; ++frame)
                rightOut[frame] = right[frame] * kAUGain;
        }
    }
}

@end
