#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>

NS_ASSUME_NONNULL_BEGIN

@interface OpalineMobileEngineBridge : NSObject

- (void)prepareWithSampleRate:(double)sampleRate maxVoices:(int)maxVoices NS_SWIFT_NAME(prepare(sampleRate:maxVoices:));
- (void)selectVoiceBank:(int)bank voice:(int)voice NS_SWIFT_NAME(selectVoiceBank(_:voice:));
- (void)selectVoiceB:(int)voice NS_SWIFT_NAME(selectVoiceB(_:));
- (NSString*)currentVoiceName;
- (NSString*)currentVoiceBName;
- (NSArray<NSString*>*)voiceBankNames;
- (NSArray<NSString*>*)voiceNamesForBank:(int)bank NS_SWIFT_NAME(voiceNamesForBank(_:));
- (BOOL)loadVoiceBankData:(NSData*)data name:(NSString*)name NS_SWIFT_NAME(loadVoiceBankData(_:name:));
- (NSData*)currentVoiceBankSysexData;
- (NSData*)voiceLibraryXMLData;
- (BOOL)loadVoiceLibraryXMLData:(NSData*)data NS_SWIFT_NAME(loadVoiceLibraryXMLData(_:));
- (void)reloadBundledFactoryBank;
- (BOOL)loadSingleVoiceXMLData:(NSData*)data fallbackName:(NSString*)name NS_SWIFT_NAME(loadSingleVoiceXMLData(_:fallbackName:));
- (BOOL)loadSingleVoiceData:(NSData*)data fileName:(NSString*)fileName NS_SWIFT_NAME(loadSingleVoiceData(_:fileName:));
- (NSData*)currentSingleVoiceXMLData;
- (NSDictionary<NSString*, NSNumber*>*)currentPatchSnapshot;
- (double)operatorRatioForIndex:(int)index NS_SWIFT_NAME(operatorRatio(for:));
- (void)initializeCurrentVoice;
- (void)copyCurrentVoice;
- (BOOL)pasteCopiedVoice;
- (BOOL)hasCopiedVoice;
- (void)storeCurrentVoice;
- (void)setPatchParameter:(NSString*)parameter value:(int)value;
- (void)setOperatorParameter:(int)operatorIndex parameter:(NSString*)parameter value:(int)value;
- (void)setOperatorEnabled:(int)operatorIndex enabled:(BOOL)enabled;
- (void)setOperatorAmpModEnabled:(int)operatorIndex enabled:(BOOL)enabled;
- (void)setEffectsEnabled:(BOOL)enabled;
- (void)setPerformanceMode:(int)mode NS_SWIFT_NAME(setPerformanceMode(_:));
- (void)setDualDetune:(int)value NS_SWIFT_NAME(setDualDetune(_:));
- (void)setSplitPoint:(int)value NS_SWIFT_NAME(setSplitPoint(_:));
- (void)setABBalance:(int)value NS_SWIFT_NAME(setABBalance(_:));
- (void)noteOn:(int)note velocity:(int)velocity NS_SWIFT_NAME(noteOn(_:velocity:));
- (void)noteOff:(int)note NS_SWIFT_NAME(noteOff(_:));
- (void)setPitchBend:(double)value;
- (void)setPitchBendRange:(int)value NS_SWIFT_NAME(setPitchBendRange(_:));
- (void)setPortamento:(int)value NS_SWIFT_NAME(setPortamento(_:));
- (void)setPortamentoModeA:(int)mode NS_SWIFT_NAME(setPortamentoModeA(_:));
- (void)setPortamentoModeB:(int)mode NS_SWIFT_NAME(setPortamentoModeB(_:));
- (void)setPortamentoFootSwitch:(BOOL)down NS_SWIFT_NAME(setPortamentoFootSwitch(_:));
- (void)setSustainPedal:(BOOL)down NS_SWIFT_NAME(setSustainPedal(_:));
- (void)setModWheelPitchRange:(int)value ampRange:(int)ampRange NS_SWIFT_NAME(setModWheelRanges(pitch:amp:));
- (void)setModWheel:(double)value;
- (void)setMonoMode:(BOOL)enabled;
- (void)setMonoModeB:(BOOL)enabled;
- (void)setScopeCaptureEnabled:(BOOL)enabled NS_SWIFT_NAME(setScopeCaptureEnabled(_:));
- (NSData*)scopeSnapshotData NS_SWIFT_NAME(scopeSnapshotData());
- (void)renderLeft:(float*)left right:(float*)right frames:(int)frames;
- (void)renderToAudioBufferList:(AudioBufferList*)audioBufferList frames:(int)frames;

@end

NS_ASSUME_NONNULL_END
