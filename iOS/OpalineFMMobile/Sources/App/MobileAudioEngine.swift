import AVFoundation

struct MobileAudioDevice: Identifiable, Equatable {
    let id: String
    let name: String
    let detail: String
}

final class MobileAudioEngine {
    private var audioEngine = AVAudioEngine()
    private let engineBridge: OpalineMobileEngineBridge
    private var sourceNode: AVAudioSourceNode?
    private var outputVolume: Float = 0.8

    var isRunning: Bool {
        audioEngine.isRunning
    }

    init(engineBridge: OpalineMobileEngineBridge) {
        self.engineBridge = engineBridge
    }

    func start() throws {
        tearDownGraph()

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
        audioEngine.mainMixerNode.outputVolume = outputVolume
        sourceNode = node

        do {
            try audioEngine.start()
        } catch {
            tearDownGraph()
            throw error
        }
    }

    func suspend() {
        audioEngine.stop()
    }

    func stop() {
        tearDownGraph()
        try? AVAudioSession.sharedInstance().setActive(false, options: [.notifyOthersOnDeactivation])
    }

    func resetAfterMediaServicesReset() {
        sourceNode = nil
        audioEngine = AVAudioEngine()
    }

    func setOutputVolume(_ value: Double) {
        outputVolume = Float(max(0, min(1, value)))
        audioEngine.mainMixerNode.outputVolume = outputVolume
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

    private func tearDownGraph() {
        audioEngine.stop()
        if let sourceNode {
            audioEngine.disconnectNodeOutput(sourceNode)
            audioEngine.detach(sourceNode)
            self.sourceNode = nil
        }
        audioEngine.reset()
    }
}
