#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>

NS_ASSUME_NONNULL_BEGIN

@interface OpalineMobileEngineBridge : NSObject

- (void)prepareWithSampleRate:(double)sampleRate maxVoices:(int)maxVoices NS_SWIFT_NAME(prepare(sampleRate:maxVoices:));
- (void)selectVoiceBank:(int)bank voice:(int)voice NS_SWIFT_NAME(selectVoiceBank(_:voice:));
- (NSString*)currentVoiceName;
- (void)noteOn:(int)note velocity:(int)velocity;
- (void)noteOff:(int)note;
- (void)setPitchBend:(double)value;
- (void)setModWheel:(double)value;
- (void)setMonoMode:(BOOL)enabled;
- (void)renderLeft:(float*)left right:(float*)right frames:(int)frames;
- (void)renderToAudioBufferList:(AudioBufferList*)audioBufferList frames:(int)frames;

@end

NS_ASSUME_NONNULL_END
