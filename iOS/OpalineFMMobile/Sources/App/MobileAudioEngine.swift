import AVFoundation

struct MobileAudioDevice: Identifiable, Equatable {
    let id: String
    let name: String
    let detail: String
}

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
            self?.engineBridge.render(to: audioBufferList, frames: Int32(frameCount))
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

    func setOutputVolume(_ value: Double) {
        audioEngine.mainMixerNode.outputVolume = Float(max(0, min(1, value)))
    }

    func currentOutputDevices() -> [MobileAudioDevice] {
        AVAudioSession.sharedInstance().currentRoute.outputs.map {
            MobileAudioDevice(id: $0.uid, name: $0.portName, detail: $0.portType.rawValue)
        }
    }

    func availableInputDevices() -> [MobileAudioDevice] {
        (AVAudioSession.sharedInstance().availableInputs ?? []).map {
            MobileAudioDevice(id: $0.uid, name: $0.portName, detail: $0.portType.rawValue)
        }
    }

    func currentInputDeviceID() -> String? {
        AVAudioSession.sharedInstance().currentRoute.inputs.first?.uid
    }

    func setPreferredInputDevice(id: String?) {
        let session = AVAudioSession.sharedInstance()
        guard let id else {
            try? session.setPreferredInput(nil)
            return
        }

        let input = session.availableInputs?.first { $0.uid == id }
        try? session.setPreferredInput(input)
    }
}
