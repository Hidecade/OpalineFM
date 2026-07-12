import Foundation

final class MobileSynthModel: ObservableObject {
    enum Screen {
        case play
        case edit
        case library
    }

    @Published var screen: Screen = .play
    @Published var bankName: String = "Factory"
    @Published var voiceName: String = "Init Voice"
    @Published var bankIndex: Int = 0
    @Published var voiceIndex: Int = 0
    @Published var isMono: Bool = false
    @Published var pitchWheel: Double = 0
    @Published var modWheel: Double = 0
    @Published var audioStatus: String = "Audio starting"

    private let engine = OpalineMobileEngineBridge()
    private let audioOutput: MobileAudioEngine

    init() {
        audioOutput = MobileAudioEngine(engineBridge: engine)
        engine.prepare(sampleRate: 44100, maxVoices: 16)
        startAudio()
        refreshVoiceName()
    }

    func selectVoice(bank: Int, voice: Int) {
        bankIndex = max(0, min(bank, 7))
        voiceIndex = max(0, min(voice, 31))
        engine.selectVoiceBank(Int32(bankIndex), voice: Int32(voiceIndex))
        refreshVoiceName()
    }

    func previousVoice() {
        selectVoice(bank: bankIndex, voice: (voiceIndex + 31) % 32)
    }

    func nextVoice() {
        selectVoice(bank: bankIndex, voice: (voiceIndex + 1) % 32)
    }

    func noteOn(_ midiNote: Int) {
        engine.noteOn(Int32(midiNote), velocity: 100)
    }

    func noteOff(_ midiNote: Int) {
        engine.noteOff(Int32(midiNote))
    }

    func setPitchWheel(_ value: Double) {
        pitchWheel = max(-1, min(1, value))
        engine.setPitchBend(pitchWheel)
    }

    func setModWheel(_ value: Double) {
        modWheel = max(0, min(1, value))
        engine.setModWheel(modWheel)
    }

    func setMono(_ enabled: Bool) {
        isMono = enabled
        engine.setMonoMode(enabled)
    }

    private func refreshVoiceName() {
        voiceName = engine.currentVoiceName()
    }

    private func startAudio() {
        do {
            try audioOutput.start()
            audioStatus = "Audio ready"
        } catch {
            audioStatus = "Audio unavailable"
        }
    }
}
