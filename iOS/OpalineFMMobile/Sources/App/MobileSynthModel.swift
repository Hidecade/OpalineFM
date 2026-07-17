import AVFoundation
import Foundation

final class MobileSynthModel: ObservableObject {
    enum Screen {
        case play
        case edit
        case library
    }

    enum PerformanceMode: Int, CaseIterable {
        case single = 0
        case dual = 1
        case split = 2

        var title: String {
            switch self {
            case .single: return "SINGLE"
            case .dual: return "DUAL"
            case .split: return "SPLIT"
            }
        }

        var next: PerformanceMode {
            switch self {
            case .single: return .dual
            case .dual: return .split
            case .split: return .single
            }
        }
    }

    enum PortamentoMode: Int {
        case off = 0
        case full = 1
        case finger = 2

        var title: String {
            switch self {
            case .off: return "PORTA"
            case .full: return "FULL"
            case .finger: return "FINGER"
            }
        }

        var isEnabled: Bool {
            self != .off
        }

        func next(mono: Bool) -> PortamentoMode {
            switch self {
            case .off:
                return .full
            case .full:
                return mono ? .finger : .off
            case .finger:
                return .off
            }
        }

        func normalized(mono: Bool) -> PortamentoMode {
            self == .finger && !mono ? .full : self
        }
    }

    @Published var screen: Screen = .play
    @Published var bankName: String = "Factory"
    @Published var voiceName: String = "Init Voice"
    @Published var voiceBName: String = "Init Voice"
    @Published var bankNames: [String] = (1...8).map { "Bank \($0)" }
    @Published var voiceNames: [String] = Array(repeating: "Voice", count: 32)
    @Published var bankIndex: Int = 0
    @Published var voiceIndex: Int = 0
    @Published var voiceBIndex: Int = 16
    @Published var performanceMode: PerformanceMode = .single
    @Published var isMono: Bool = false
    @Published var isMonoB: Bool = false
    @Published var portamentoModeA: PortamentoMode = .off
    @Published var portamentoModeB: PortamentoMode = .off
    @Published var dualDetune: Int = 0
    @Published var splitPoint: Int = 60
    @Published var pitchWheel: Double = 0
    @Published var modWheel: Double = 0
    @Published var pitchBendRange: Int = 2
    @Published var portamento: Int = 0
    @Published var modWheelPitchRange: Int = 0
    @Published var modWheelAmpRange: Int = 0
    @Published var masterVolume: Double = 0.8
    @Published var transpose: Double = 0
    @Published var balance: Double = 0
    @Published var audioStatus: String = "Audio starting"
    @Published var scopeSamples: [Float] = Array(repeating: 0, count: 128)
    @Published var editValues: [String: Int] = [:]
    @Published var canPasteVoice: Bool = false
    @Published var midiInputDevices: [MobileMIDIInputDevice] = []
    @Published var selectedMIDIInputID: Int32?
    @Published var midiReceiveChannel: Int?
    @Published var audioOutputDevices: [MobileAudioDevice] = []
    @Published var audioInputDevices: [MobileAudioDevice] = []
    @Published var selectedAudioInputID: String?
    @Published var externalActiveNotes: [Int: Int] = [:]

    private let engine = OpalineMobileEngineBridge()
    private let audioOutput: MobileAudioEngine
    private var midiInput: MobileMIDIInput?
    private var scopeTimer: Timer?
    private var libraryAutosaveTimer: Timer?
    private var audioRouteObserver: NSObjectProtocol?
    private var activeTransposedNotes: [Int: [Int]] = [:]
    private var externalNoteCounts: [Int: Int] = [:]

    init() {
        audioOutput = MobileAudioEngine(engineBridge: engine)
        engine.prepare(sampleRate: 44100, maxVoices: 16)
        loadPersistedVoiceLibrary()
        engine.reloadBundledFactoryBank()
        saveVoiceLibraryState()
        engine.selectVoiceB(Int32(voiceBIndex))
        applyWheelRanges()
        startAudio()
        refreshBankNames()
        refreshVoiceNames()
        refreshVoiceName()
        refreshVoiceBName()
        refreshEditValues()
        midiInput = MobileMIDIInput(synth: self)
        midiInput?.onDevicesChanged = { [weak self] devices in
            self?.midiInputDevices = devices
        }
        refreshMIDISettings()
        refreshAudioDevices()
        observeAudioRouteChanges()
        startScopeUpdates()
    }

    deinit {
        scopeTimer?.invalidate()
        libraryAutosaveTimer?.invalidate()
        if let audioRouteObserver {
            NotificationCenter.default.removeObserver(audioRouteObserver)
        }
    }

    func selectVoice(bank: Int, voice: Int) {
        bankIndex = max(0, min(bank, 7))
        voiceIndex = max(0, min(voice, 31))
        engine.selectVoiceBank(Int32(bankIndex), voice: Int32(voiceIndex))
        refreshBankName()
        refreshVoiceNames()
        refreshVoiceName()
        refreshVoiceBName()
        refreshEditValues()
        scheduleLibraryAutosave()
    }

    func selectBank(_ bank: Int) {
        selectVoice(bank: bank, voice: voiceIndex)
    }

    func selectVoiceB(_ voice: Int) {
        voiceBIndex = max(0, min(voice, 31))
        engine.selectVoiceB(Int32(voiceBIndex))
        refreshVoiceBName()
    }

    func previousVoice() {
        selectVoice(bank: bankIndex, voice: (voiceIndex + 31) % 32)
    }

    func nextVoice() {
        selectVoice(bank: bankIndex, voice: (voiceIndex + 1) % 32)
    }

    func previousVoiceB() {
        selectVoiceB((voiceBIndex + 31) % 32)
    }

    func nextVoiceB() {
        selectVoiceB((voiceBIndex + 1) % 32)
    }

    func cyclePerformanceMode() {
        setPerformanceMode(performanceMode.next)
    }

    func setPerformanceMode(_ mode: PerformanceMode) {
        performanceMode = mode
        engine.setPerformanceMode(Int32(mode.rawValue))
        scheduleLibraryAutosave()
    }

    func setDualDetune(_ value: Int) {
        dualDetune = max(-16, min(16, value))
        engine.setDualDetune(Int32(dualDetune))
        scheduleLibraryAutosave()
    }

    func setSplitPoint(_ value: Int) {
        splitPoint = max(0, min(127, value))
        engine.setSplitPoint(Int32(splitPoint))
        scheduleLibraryAutosave()
    }

    func noteOn(_ midiNote: Int, velocity: Int = 100) {
        let transposedNote = max(0, min(127, midiNote + Int(transpose.rounded())))
        activeTransposedNotes[midiNote, default: []].append(transposedNote)
        engine.noteOn(Int32(transposedNote), velocity: Int32(max(1, min(127, velocity))))
    }

    func noteOff(_ midiNote: Int) {
        let transposedNote = activeTransposedNotes[midiNote]?.popLast() ?? max(0, min(127, midiNote + Int(transpose.rounded())))
        if activeTransposedNotes[midiNote]?.isEmpty == true {
            activeTransposedNotes.removeValue(forKey: midiNote)
        }
        engine.noteOff(Int32(transposedNote))
    }

    func midiNoteOn(_ midiNote: Int, velocity: Int) {
        let safeNote = max(0, min(127, midiNote))
        let safeVelocity = max(1, min(127, velocity))
        externalNoteCounts[safeNote, default: 0] += 1
        externalActiveNotes[safeNote] = safeVelocity
        noteOn(safeNote, velocity: safeVelocity)
    }

    func midiNoteOff(_ midiNote: Int) {
        let safeNote = max(0, min(127, midiNote))
        let count = max(0, (externalNoteCounts[safeNote] ?? 0) - 1)
        if count == 0 {
            externalNoteCounts.removeValue(forKey: safeNote)
            externalActiveNotes.removeValue(forKey: safeNote)
        } else {
            externalNoteCounts[safeNote] = count
        }
        noteOff(safeNote)
    }

    func setPitchWheel(_ value: Double) {
        pitchWheel = max(-1, min(1, value))
        engine.setPitchBend(pitchWheel)
    }

    func setModWheel(_ value: Double) {
        modWheel = max(0, min(1, value))
        engine.setModWheel(modWheel)
    }

    func setSustainPedal(_ down: Bool) {
        engine.setSustainPedal(down)
    }

    func setPortamentoFootSwitch(_ down: Bool) {
        engine.setPortamentoFootSwitch(down)
    }

    func refreshMIDISettings() {
        midiInput?.refreshDevicesAndConnections()
    }

    func selectMIDIInput(id: Int32?) {
        selectedMIDIInputID = id
        midiInput?.setSelectedInput(id: id)
    }

    func setMIDIReceiveChannel(_ channel: Int?) {
        midiReceiveChannel = channel.map { max(1, min(16, $0)) }
        midiInput?.setReceiveChannel(midiReceiveChannel)
    }

    func refreshAudioDevices() {
        audioOutputDevices = audioOutput.currentOutputDevices()
        audioInputDevices = audioOutput.availableInputDevices()
        selectedAudioInputID = audioOutput.currentInputDeviceID()
    }

    func selectAudioInput(id: String?) {
        selectedAudioInputID = id
        audioOutput.setPreferredInputDevice(id: id)
        refreshAudioDevices()
    }

    func setPitchBendRange(_ value: Int) {
        pitchBendRange = max(0, min(12, value))
        engine.setPitchBendRange(Int32(pitchBendRange))
        scheduleLibraryAutosave()
    }

    func setPortamento(_ value: Int) {
        portamento = max(0, min(99, value))
        engine.setPortamento(Int32(portamento))
        scheduleLibraryAutosave()
    }

    func setModWheelPitchRange(_ value: Int) {
        modWheelPitchRange = max(0, min(99, value))
        applyWheelRanges()
        scheduleLibraryAutosave()
    }

    func setModWheelAmpRange(_ value: Int) {
        modWheelAmpRange = max(0, min(99, value))
        applyWheelRanges()
        scheduleLibraryAutosave()
    }

    func setMasterVolume(_ value: Double) {
        masterVolume = max(0, min(1, value))
        audioOutput.setOutputVolume(masterVolume)
    }

    func setTranspose(_ value: Double) {
        transpose = max(-24, min(24, value))
        scheduleLibraryAutosave()
    }

    func setBalance(_ value: Double) {
        balance = max(-100, min(100, value))
        engine.setABBalance(Int32(balance.rounded()))
        scheduleLibraryAutosave()
    }

    func setMono(_ enabled: Bool) {
        isMono = enabled
        portamentoModeA = portamentoModeA.normalized(mono: isMono)
        engine.setMonoMode(enabled)
        engine.setPortamentoModeA(Int32(portamentoModeA.rawValue))
        scheduleLibraryAutosave()
    }

    func setMonoB(_ enabled: Bool) {
        isMonoB = enabled
        portamentoModeB = portamentoModeB.normalized(mono: isMonoB)
        engine.setMonoModeB(enabled)
        engine.setPortamentoModeB(Int32(portamentoModeB.rawValue))
        scheduleLibraryAutosave()
    }

    func cyclePortamentoModeA() {
        portamentoModeA = portamentoModeA.next(mono: isMono)
        engine.setPortamentoModeA(Int32(portamentoModeA.rawValue))
        scheduleLibraryAutosave()
    }

    func cyclePortamentoModeB() {
        portamentoModeB = portamentoModeB.next(mono: isMonoB)
        engine.setPortamentoModeB(Int32(portamentoModeB.rawValue))
        scheduleLibraryAutosave()
    }

    func refreshEditValues() {
        editValues = engine.currentPatchSnapshot().reduce(into: [:]) { result, item in
            result[item.key] = item.value.intValue
        }
    }

    func editValue(_ key: String, fallback: Int = 0) -> Int {
        editValues[key] ?? fallback
    }

    func operatorRatioText(for index: Int) -> String {
        String(format: "%.2f", engine.operatorRatio(for: Int32(index)))
    }

    func setEditValue(_ key: String, _ value: Int) {
        editValues[key] = value
        engine.setPatchParameter(key, value: Int32(value))
        scheduleLibraryAutosave()
    }

    func setOperatorEditValue(operatorIndex: Int, key: String, value: Int) {
        let fullKey = "op\(operatorIndex + 1).\(key)"
        editValues[fullKey] = value
        engine.setOperatorParameter(Int32(operatorIndex), parameter: key, value: Int32(value))
        scheduleLibraryAutosave()
    }

    func setOperatorEnabled(operatorIndex: Int, enabled: Bool) {
        editValues["op\(operatorIndex + 1).enabled"] = enabled ? 1 : 0
        engine.setOperatorEnabled(Int32(operatorIndex), enabled: enabled)
        scheduleLibraryAutosave()
    }

    func setOperatorAmpModEnabled(operatorIndex: Int, enabled: Bool) {
        editValues["op\(operatorIndex + 1).am"] = enabled ? 1 : 0
        engine.setOperatorAmpModEnabled(Int32(operatorIndex), enabled: enabled)
        scheduleLibraryAutosave()
    }

    func setEffectsEnabled(_ enabled: Bool) {
        editValues["fx.enabled"] = enabled ? 1 : 0
        engine.setEffectsEnabled(enabled)
        scheduleLibraryAutosave()
    }

    func loadSingleVoice(data: Data, fileName: String) -> Bool {
        let loaded = engine.loadSingleVoiceData(data, fileName: fileName)
        if loaded {
            refreshVoiceName()
            refreshEditValues()
            scheduleLibraryAutosave()
        }
        return loaded
    }

    func currentSingleVoiceXMLData() -> Data {
        engine.currentSingleVoiceXMLData()
    }

    func initializeCurrentVoice() {
        engine.initializeCurrentVoice()
        refreshVoiceName()
        refreshEditValues()
        scheduleLibraryAutosave()
    }

    func copyCurrentVoice() {
        engine.copyCurrentVoice()
        canPasteVoice = engine.hasCopiedVoice()
    }

    func pasteCopiedVoice() {
        guard engine.pasteCopiedVoice() else { return }
        canPasteVoice = engine.hasCopiedVoice()
        refreshVoiceName()
        refreshEditValues()
        scheduleLibraryAutosave()
    }

    func storeCurrentVoice() {
        engine.storeCurrentVoice()
        refreshVoiceNames()
        refreshVoiceName()
        refreshEditValues()
        saveVoiceLibraryState()
    }

    func loadVoiceBank(data: Data, fileName: String) -> Bool {
        let loaded = engine.loadVoiceBankData(data, name: fileName)
        if loaded {
            refreshBankNames()
            refreshBankName()
            refreshVoiceNames()
            refreshVoiceName()
            refreshVoiceBName()
            refreshEditValues()
            saveVoiceLibraryState()
        }
        return loaded
    }

    func currentVoiceBankSysexData() -> Data {
        let data = engine.currentVoiceBankSysexData()
        refreshVoiceNames()
        refreshVoiceName()
        return data
    }

    func voiceLibraryXMLData() -> Data {
        let data = engine.voiceLibraryXMLData()
        refreshVoiceNames()
        refreshVoiceName()
        return data
    }

    private func loadPersistedVoiceLibrary() {
        do {
            let data = try Data(contentsOf: voiceLibraryStateURL)
            guard engine.loadVoiceLibraryXMLData(data) else { return }
            audioStatus = "Library restored"
        } catch {
            // First launch or missing state file: keep the bundled factory bank.
        }
    }

    private func scheduleLibraryAutosave() {
        libraryAutosaveTimer?.invalidate()
        libraryAutosaveTimer = Timer.scheduledTimer(withTimeInterval: 0.8, repeats: false) { [weak self] _ in
            self?.saveVoiceLibraryState()
        }
    }

    private func saveVoiceLibraryState() {
        libraryAutosaveTimer?.invalidate()
        libraryAutosaveTimer = nil
        do {
            let directory = voiceLibraryStateURL.deletingLastPathComponent()
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            let data = engine.voiceLibraryXMLData()
            try data.write(to: voiceLibraryStateURL, options: .atomic)
        } catch {
            audioStatus = "Library save failed"
        }
    }

    private var voiceLibraryStateURL: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? FileManager.default.temporaryDirectory
        return base
            .appendingPathComponent("OpalineFM", isDirectory: true)
            .appendingPathComponent("VoiceLibrary.opalinelibrary.xml")
    }

    private func refreshVoiceName() {
        voiceName = engine.currentVoiceName()
    }

    private func refreshVoiceBName() {
        voiceBName = engine.currentVoiceBName()
    }

    private func refreshBankNames() {
        bankNames = engine.voiceBankNames().map { $0 as String }
        refreshBankName()
    }

    private func refreshBankName() {
        bankName = bankIndex < bankNames.count ? bankNames[bankIndex] : "Bank \(bankIndex + 1)"
    }

    private func refreshVoiceNames() {
        voiceNames = engine.voiceNamesForBank(Int32(bankIndex)).map { $0 as String }
    }

    private func applyWheelRanges() {
        engine.setModWheelRanges(pitch: Int32(modWheelPitchRange), amp: Int32(modWheelAmpRange))
    }

    private func startAudio() {
        do {
            try audioOutput.start()
            audioOutput.setOutputVolume(masterVolume)
            audioStatus = "Audio ready"
        } catch {
            audioStatus = "Audio unavailable"
        }
    }

    private func startScopeUpdates() {
        scopeTimer?.invalidate()
        scopeTimer = Timer.scheduledTimer(withTimeInterval: 1.0 / 20.0, repeats: true) { [weak self] _ in
            guard let self else { return }
            let samples = self.engine.scopeSnapshot().map { $0.floatValue }
            if samples.count == self.scopeSamples.count {
                guard samples != self.scopeSamples else { return }
                self.scopeSamples = samples
            }
        }
    }

    private func observeAudioRouteChanges() {
        audioRouteObserver = NotificationCenter.default.addObserver(
            forName: AVAudioSession.routeChangeNotification,
            object: AVAudioSession.sharedInstance(),
            queue: .main
        ) { [weak self] _ in
            self?.refreshAudioDevices()
        }
    }
}
