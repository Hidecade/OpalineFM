import AVFoundation

final class MobileAudioEngine {
    private let audioEngine = AVAudioEngine()
    private let engineBridge: OpalineMobileEngineBridge
    private var sourceNode: AVAudioSourceNode?

    init(engineBridge: OpalineMobileEngineBridge) {
        self.engineBridge = engineBridge
    }

    func start() throws {
        let session = AVAudioSession.sharedInstance()
        try session.setCategory(.playback, mode: .default, options: [.mixWithOthers])
        try session.setPreferredSampleRate(44_100)
        try session.setPreferredIOBufferDuration(0.005)
        try session.setActive(true)

        let sampleRate = session.sampleRate > 0 ? session.sampleRate : 44_100
        engineBridge.prepare(sampleRate: sampleRate, maxVoices: 16)

        let format = AVAudioFormat(standardFormatWithSampleRate: sampleRate, channels: 2)!
        let node = AVAudioSourceNode { [weak self] _, _, frameCount, audioBufferList in
            self?.engineBridge.render(toAudioBufferList: audioBufferList, frames: Int32(frameCount))
            return noErr
        }

        audioEngine.attach(node)
        audioEngine.connect(node, to: audioEngine.mainMixerNode, format: format)
        sourceNode = node

        try audioEngine.start()
    }

    func stop() {
        audioEngine.stop()
        try? AVAudioSession.sharedInstance().setActive(false)
    }
}
