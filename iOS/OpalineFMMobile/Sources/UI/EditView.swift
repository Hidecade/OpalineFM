import AVKit
import SwiftUI
import UIKit

struct EditView: View {
    @EnvironmentObject private var synth: MobileSynthModel
    @State private var selectedTab: EditTab = .op1
    @State private var values: [String: Int] = EditParameterDefaults.values
    @State private var lfoWave = "TRIANGLE"
    @State private var syncEnabled = false

    var body: some View {
        GeometryReader { proxy in
            VStack(spacing: 10) {
                editHeader

                HStack(alignment: .top, spacing: 8) {
                    tabBar
                        .frame(width: EditLayout.tabWidth)

                    ZStack(alignment: .topLeading) {
                        Color.clear

                        currentEditPage
                            .frame(width: EditLayout.totalPanelWidth, alignment: .topLeading)
                            .frame(maxHeight: .infinity, alignment: .top)
                    }
                    .frame(width: EditLayout.totalPanelWidth)
                    .frame(maxHeight: .infinity)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            .padding(8)
            .frame(width: proxy.size.width, height: proxy.size.height)
            .background(EditBackPanel())
            .background(EditSkin.appBackground)
            .onAppear {
                synth.refreshEditValues()
                values = synth.editValues
                lfoWave = waveName(values["lfo.wave"] ?? 0)
                syncEnabled = (values["lfo.sync"] ?? 0) != 0
                synth.refreshMIDISettings()
                synth.refreshAudioDevices()
            }
        }
    }

    @ViewBuilder
    private var currentEditPage: some View {
        switch selectedTab {
        case .op1, .op2, .op3, .op4:
            OperatorEditPage(index: selectedTab.operatorIndex ?? 0, values: $values)
        case .lfoPitchEg:
            LfoPitchEgEditPage(values: $values, lfoWave: $lfoWave, syncEnabled: $syncEnabled)
        case .fx:
            FxEditPage(values: $values)
        case .setting:
            SettingEditPage()
        }
    }

    private var editHeader: some View {
        HStack(spacing: 8) {
            playEditSwitch
                .frame(width: 132, height: 28)

            Spacer(minLength: 0)

            AuditionKeyRow()
                .frame(width: 336, height: 52)

            EditScopeView(samples: synth.scopeSamples)
                .frame(minWidth: 120, maxWidth: 220)
                .frame(height: 52)
        }
        .frame(height: 56)
    }

    private var playEditSwitch: some View {
        HStack(spacing: 2) {
            Button("PLAY") {
                synth.screen = .play
            }
            .buttonStyle(EditPanelButtonStyle(active: false))

            Button("EDIT") {}
                .buttonStyle(EditPanelButtonStyle(active: true))
        }
        .padding(2)
        .background(
            RoundedRectangle(cornerRadius: 4)
                .fill(Color(hexValue: 0x070806))
        )
        .overlay(RoundedRectangle(cornerRadius: 4).stroke(EditSkin.panelBorder, lineWidth: 1))
    }

    private var tabBar: some View {
        VStack(spacing: 4) {
            Text(String(format: "%2d %@", synth.voiceIndex + 1, synth.voiceName))
                .font(.system(size: 11, weight: .bold, design: .monospaced))
                .foregroundStyle(EditSkin.lcdOn)
                .lineLimit(1)
                .minimumScaleFactor(0.58)
                .frame(maxWidth: .infinity, minHeight: 26)
                .padding(.horizontal, 4)
                .background(
                    RoundedRectangle(cornerRadius: 3)
                        .fill(Color(hexValue: 0x071423))
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 3)
                        .stroke(Color(hexValue: 0x2462ad), lineWidth: 1)
                )

            ForEach(EditTab.allCases) { tab in
                Button(tab.title) {
                    selectedTab = tab
                }
                .buttonStyle(EditPanelButtonStyle(active: selectedTab == tab))
                .frame(height: 34)
            }

            Spacer(minLength: 0)
        }
    }

    private func waveName(_ index: Int) -> String {
        ["SAW UP", "SQUARE", "TRIANGLE", "S/H"][max(0, min(3, index))]
    }
}

private struct EditScopeView: View {
    let samples: [Float]

    var body: some View {
        Canvas { context, size in
            guard size.width > 0, size.height > 0 else { return }

            let window = CGRect(origin: .zero, size: size)
            context.fill(Path(roundedRect: window, cornerRadius: 4), with: .linearGradient(
                Gradient(colors: [Color(hexValue: 0x0b2037), Color(hexValue: 0x03101f)]),
                startPoint: CGPoint(x: window.midX, y: window.minY),
                endPoint: CGPoint(x: window.midX, y: window.maxY)
            ))
            context.stroke(Path(roundedRect: window.insetBy(dx: 0.5, dy: 0.5), cornerRadius: 4),
                           with: .color(Color(hexValue: 0x2462ad)),
                           lineWidth: 1)

            guard samples.count > 1 else { return }

            let rect = window.insetBy(dx: 6, dy: 6)
            var path = Path()
            for index in samples.indices {
                let x = rect.minX + rect.width * CGFloat(index) / CGFloat(samples.count - 1)
                let value = max(-1, min(1, CGFloat(samples[index])))
                let y = rect.midY - value * rect.height * 0.44
                if index == samples.startIndex {
                    path.move(to: CGPoint(x: x, y: y))
                } else {
                    path.addLine(to: CGPoint(x: x, y: y))
                }
            }

            context.stroke(path, with: .color(EditSkin.lcdOn.opacity(0.30)), lineWidth: 4.2)
            context.stroke(path, with: .color(EditSkin.lcdOn.opacity(0.95)), lineWidth: 1.7)
        }
        .accessibilityHidden(true)
    }
}

private struct AuditionKeyRow: View {
    @EnvironmentObject private var synth: MobileSynthModel
    @State private var baseNote = 48
    @State private var activeNotes: [Int: Int] = [:]

    private let noteCount = 13

    var body: some View {
        HStack(spacing: 5) {
            octaveButtons
                .frame(width: 42)

            GeometryReader { _ in
                ZStack {
                    Canvas { context, size in
                        drawKeyboard(context: &context, size: size)
                    }

                    MiniKeyboardTouchSurface(
                        synth: synth,
                        baseNote: baseNote,
                        activeNotes: $activeNotes,
                        noteCount: noteCount
                    )
                }
                .onDisappear {
                    stopActiveNotes()
                }
            }
        }
    }

    private var octaveButtons: some View {
        VStack(spacing: 4) {
            Button("+") {
                shiftOctave(1)
            }
            .buttonStyle(AuditionOctaveButtonStyle())
            .disabled(baseNote >= maxBaseNote || !activeNotes.isEmpty)

            Button("-") {
                shiftOctave(-1)
            }
            .buttonStyle(AuditionOctaveButtonStyle())
            .disabled(baseNote <= minBaseNote || !activeNotes.isEmpty)
        }
    }

    private var displayedActiveNotes: [Int: Int] {
        activeNotes.merging(synth.externalActiveNotes) { touchVelocity, midiVelocity in
            max(touchVelocity, midiVelocity)
        }
    }

    private func drawKeyboard(context: inout GraphicsContext, size: CGSize) {
        guard size.width > 0, size.height > 0 else { return }

        let keyArea = CGRect(origin: .zero, size: size)
        let whites = whiteNotes
        let whiteWidth = keyArea.width / CGFloat(max(1, whites.count))
        let blackHeight = keyArea.height * 0.62
        let blackWidth = whiteWidth * 0.56

        for (index, offset) in whites.enumerated() {
            let note = baseNote + offset
            let held = displayedActiveNotes[note] != nil
            let rect = CGRect(
                x: keyArea.minX + CGFloat(index) * whiteWidth + 0.5,
                y: keyArea.minY,
                width: max(1, whiteWidth - 1),
                height: max(1, keyArea.height - 1)
            )
            context.fill(Path(rect), with: .linearGradient(
                Gradient(colors: held
                         ? [Color(hexValue: 0xb2efff), Color(hexValue: 0x70cde7)]
                         : [Color(hexValue: 0xf4eee1), Color(hexValue: 0xd8cdb7)]),
                startPoint: CGPoint(x: rect.midX, y: rect.minY),
                endPoint: CGPoint(x: rect.midX, y: rect.maxY)
            ))
            context.stroke(Path(roundedRect: rect, cornerRadius: 2), with: .color(Color.black.opacity(0.84)), lineWidth: 1)
            context.fill(Path(CGRect(x: rect.minX + 1.4, y: rect.minY, width: max(1, rect.width - 2.8), height: 4)),
                         with: .color(Color.white.opacity(held ? 0.16 : 0.25)))
            context.fill(Path(CGRect(x: rect.minX, y: rect.maxY - 3.5, width: rect.width, height: 3.5)),
                         with: .color(Color.black.opacity(0.18)))

            if note % 12 == 0 {
                drawCLabel(note: note, in: rect, context: &context, held: held)
            }
        }

        for offset in blackNotes {
            let note = baseNote + offset
            let held = displayedActiveNotes[note] != nil
            let whiteBefore = whites.filter { $0 < offset }.count
            let x = max(keyArea.minX, min(keyArea.maxX - blackWidth, keyArea.minX + CGFloat(whiteBefore) * whiteWidth - blackWidth * 0.5))
            let rect = CGRect(x: x, y: keyArea.minY, width: blackWidth, height: blackHeight).insetBy(dx: 1, dy: 0)
            let path = Path(roundedRect: rect, cornerRadius: 1.5)
            context.drawLayer { layer in
                layer.addFilter(.shadow(color: .black.opacity(0.70), radius: 2, x: 0, y: 2.5))
                layer.fill(path, with: .linearGradient(
                    Gradient(colors: held
                             ? [Color(hexValue: 0x284650), Color(hexValue: 0x0d8aa8)]
                             : [Color(hexValue: 0x2f302b), Color(hexValue: 0x11120f)]),
                    startPoint: CGPoint(x: rect.midX, y: rect.minY),
                    endPoint: CGPoint(x: rect.midX, y: rect.maxY)
                ))
            }
            context.stroke(path, with: .color(Color.black.opacity(0.95)), lineWidth: 1)
            let highlight = CGRect(
                x: rect.minX + 3,
                y: rect.minY + 3,
                width: max(1, rect.width - 6),
                height: rect.height * 0.18
            )
            context.fill(Path(roundedRect: highlight, cornerRadius: 1),
                         with: .color(Color.white.opacity(held ? 0.15 : 0.07)))
        }
    }

    private func shiftOctave(_ delta: Int) {
        stopActiveNotes()
        baseNote = clampBaseNote(baseNote + delta * 12)
    }

    private func clampBaseNote(_ note: Int) -> Int {
        max(minBaseNote, min(maxBaseNote, note))
    }

    private var minBaseNote: Int { 24 }
    private var maxBaseNote: Int { 96 }

    private func stopActiveNotes() {
        for note in activeNotes.keys {
            synth.noteOff(note)
        }
        activeNotes.removeAll()
    }

    private func drawCLabel(note: Int, in rect: CGRect, context: inout GraphicsContext, held: Bool) {
        let octave = note / 12 - 1
        let text = context.resolve(
            Text("C\(octave)")
                .font(.system(size: 8, weight: .bold, design: .monospaced))
                .foregroundColor(held ? Color(hexValue: 0x0b2630).opacity(0.82) : Color(hexValue: 0x4b473d))
        )
        context.draw(text, at: CGPoint(x: rect.midX, y: rect.maxY - 10), anchor: .center)
    }

    private var whiteNotes: [Int] {
        (0..<noteCount).filter { !isBlack(note: baseNote + $0) }
    }

    private var blackNotes: [Int] {
        (0..<noteCount).filter { isBlack(note: baseNote + $0) }
    }

    private func isBlack(note: Int) -> Bool {
        [1, 3, 6, 8, 10].contains(note % 12)
    }
}

private struct MiniKeyboardTouchSurface: UIViewRepresentable {
    let synth: MobileSynthModel
    let baseNote: Int
    @Binding var activeNotes: [Int: Int]
    let noteCount: Int

    func makeUIView(context: Context) -> MiniKeyboardTouchSurfaceView {
        let view = MiniKeyboardTouchSurfaceView()
        view.synth = synth
        view.baseNote = baseNote
        view.noteCount = noteCount
        view.onActiveNotesChanged = { activeNotes = $0 }
        return view
    }

    func updateUIView(_ uiView: MiniKeyboardTouchSurfaceView, context: Context) {
        uiView.synth = synth
        uiView.baseNote = baseNote
        uiView.noteCount = noteCount
        uiView.onActiveNotesChanged = { activeNotes = $0 }
    }
}

private final class MiniKeyboardTouchSurfaceView: UIView {
    weak var synth: MobileSynthModel?
    var baseNote = 48
    var noteCount = 13
    var onActiveNotesChanged: (([Int: Int]) -> Void)?

    private var touchNotes: [UITouch: Int] = [:]
    private var noteCounts: [Int: Int] = [:]
    private var noteVelocities: [Int: Int] = [:]

    override init(frame: CGRect) {
        super.init(frame: frame)
        isMultipleTouchEnabled = true
        backgroundColor = .clear
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        isMultipleTouchEnabled = true
        backgroundColor = .clear
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches {
            guard let note = note(at: touch.location(in: self)) else { continue }
            touchNotes[touch] = note
            start(note: note)
        }
        emitActiveNotes()
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches {
            let nextNote = note(at: touch.location(in: self))
            let previousNote = touchNotes[touch]
            guard previousNote != nextNote else { continue }

            if let previousNote {
                stop(note: previousNote)
            }
            if let nextNote {
                touchNotes[touch] = nextNote
                start(note: nextNote)
            } else {
                touchNotes.removeValue(forKey: touch)
            }
        }
        emitActiveNotes()
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        finish(touches)
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        finish(touches)
    }

    private func finish(_ touches: Set<UITouch>) {
        for touch in touches {
            guard let note = touchNotes.removeValue(forKey: touch) else { continue }
            stop(note: note)
        }
        emitActiveNotes()
    }

    private func start(note: Int) {
        let count = noteCounts[note] ?? 0
        noteCounts[note] = count + 1
        noteVelocities[note] = 104
        if count == 0 {
            synth?.noteOn(note, velocity: 104)
        }
    }

    private func stop(note: Int) {
        let count = max(0, (noteCounts[note] ?? 0) - 1)
        if count == 0 {
            noteCounts.removeValue(forKey: note)
            noteVelocities.removeValue(forKey: note)
            synth?.noteOff(note)
        } else {
            noteCounts[note] = count
        }
    }

    private func emitActiveNotes() {
        onActiveNotesChanged?(noteVelocities)
    }

    private func note(at location: CGPoint) -> Int? {
        guard bounds.width > 0, bounds.height > 0 else { return nil }
        let keyArea = CGRect(origin: .zero, size: bounds.size)
        guard keyArea.contains(location) else { return nil }

        let whites = whiteNotes
        let whiteWidth = keyArea.width / CGFloat(max(1, whites.count))
        let blackHeight = keyArea.height * 0.62
        let blackWidth = whiteWidth * 0.56

        if location.y <= keyArea.minY + blackHeight {
            for offset in blackNotes {
                let whiteBefore = whites.filter { $0 < offset }.count
                let x = max(keyArea.minX, min(keyArea.maxX - blackWidth, keyArea.minX + CGFloat(whiteBefore) * whiteWidth - blackWidth * 0.5))
                if CGRect(x: x, y: keyArea.minY, width: blackWidth, height: blackHeight).contains(location) {
                    return baseNote + offset
                }
            }
        }

        let whiteIndex = min(max(0, Int((location.x - keyArea.minX) / max(1, whiteWidth))), whites.count - 1)
        return baseNote + whites[whiteIndex]
    }

    private var whiteNotes: [Int] {
        (0..<noteCount).filter { !isBlack(note: baseNote + $0) }
    }

    private var blackNotes: [Int] {
        (0..<noteCount).filter { isBlack(note: baseNote + $0) }
    }

    private func isBlack(note: Int) -> Bool {
        [1, 3, 6, 8, 10].contains(note % 12)
    }
}

private struct AuditionOctaveButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 14, weight: .heavy, design: .monospaced))
            .foregroundStyle(Color(hexValue: 0xf4eee1))
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(
                RoundedRectangle(cornerRadius: 3)
                    .fill(configuration.isPressed ? Color(hexValue: 0x168e78) : Color(hexValue: 0x2b2c27))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 3)
                    .stroke(configuration.isPressed ? Color(hexValue: 0x27e3c4) : EditSkin.panelBorder, lineWidth: 1)
            )
            .opacity(configuration.isPressed ? 0.92 : 1.0)
    }
}

private enum EditTab: String, CaseIterable, Identifiable {
    case op1
    case op2
    case op3
    case op4
    case lfoPitchEg
    case fx
    case setting

    var id: String { rawValue }

    var title: String {
        switch self {
        case .op1: return "OP 1"
        case .op2: return "OP 2"
        case .op3: return "OP 3"
        case .op4: return "OP 4"
        case .lfoPitchEg: return "LFO/PITCH EG"
        case .fx: return "FX"
        case .setting: return "SETTING"
        }
    }

    var operatorIndex: Int? {
        switch self {
        case .op1: return 0
        case .op2: return 1
        case .op3: return 2
        case .op4: return 3
        default: return nil
        }
    }
}

private struct AlgorithmEditPage: View {
    @EnvironmentObject private var synth: MobileSynthModel
    @Binding var values: [String: Int]

    var body: some View {
        HStack(alignment: .top, spacing: 8) {
            EditPanel(title: "ALG") {
                HStack(alignment: .top, spacing: 10) {
                    AlgorithmDiagram(algorithm: value("alg"), feedback: value("fb"))
                        .frame(width: 112, height: 122)

                    VStack(spacing: 8) {
                        EditKnob(title: "ALG", value: binding("alg"), range: 1...8, defaultValue: EditParameterDefaults.values["alg"])
                        EditKnob(title: "FB", value: binding("fb"), range: 0...7, defaultValue: EditParameterDefaults.values["fb"])
                    }
                }
            }
            .frame(width: 220)

            EditPanel(title: "PITCH EG") {
                HStack(alignment: .top, spacing: 10) {
                    PitchEgGraph(values: values)
                        .frame(width: 174, height: 124)

                    VStack(spacing: 8) {
                        HStack(spacing: 8) {
                            ForEach(["PR1", "PR2", "PR3"], id: \.self) { key in
                                EditKnob(title: key, value: binding(key.lowercased()), range: 0...99, defaultValue: EditParameterDefaults.values[key.lowercased()])
                            }
                        }
                        HStack(spacing: 8) {
                            ForEach(["PL1", "PL2", "PL3"], id: \.self) { key in
                                EditKnob(title: key, value: binding(key.lowercased()), range: 0...99, defaultValue: EditParameterDefaults.values[key.lowercased()])
                            }
                        }
                    }
                }
            }
            .frame(width: 520)
        }
    }

    private func value(_ key: String) -> Int {
        values[key] ?? 0
    }

    private func binding(_ key: String) -> Binding<Int> {
        Binding(
            get: { values[key] ?? 0 },
            set: {
                values[key] = $0
                synth.setEditValue(key, $0)
            }
        )
    }
}

private struct LfoPitchEgEditPage: View {
    @EnvironmentObject private var synth: MobileSynthModel
    @Binding var values: [String: Int]
    @Binding var lfoWave: String
    @Binding var syncEnabled: Bool
    @State private var showingWavePicker = false

    var body: some View {
        ZStack {
            HStack(alignment: .top, spacing: 8) {
                EditPanel(title: "LFO") {
                    VStack(alignment: .center, spacing: 8) {
                        VStack(alignment: .leading, spacing: 6) {
                            HStack(spacing: 7) {
                                Text("WAVE")
                                    .font(.system(size: 13, weight: .bold))
                                    .foregroundStyle(EditSkin.textMuted)
                                    .frame(width: 70, alignment: .leading)

                                Button {
                                    showingWavePicker = true
                                } label: {
                                    DropdownLook(text: lfoWave)
                                }
                                .buttonStyle(.plain)
                                .frame(maxWidth: .infinity)
                                .frame(height: 36)
                            }

                            HStack(spacing: 7) {
                                Text("SYNC")
                                    .font(.system(size: 13, weight: .bold))
                                    .foregroundStyle(EditSkin.textMuted)
                                    .frame(width: 70, alignment: .leading)

                                HStack {
                                    SyncToggle(isOn: Binding(
                                        get: { syncEnabled },
                                        set: {
                                            syncEnabled = $0
                                            values["lfo.sync"] = $0 ? 1 : 0
                                            synth.setEditValue("lfo.sync", $0 ? 1 : 0)
                                        }
                                    ))
                                    .frame(width: 28, height: 28, alignment: .center)

                                    Spacer(minLength: 0)
                                }
                                .frame(maxWidth: .infinity, alignment: .leading)
                            }
                        }
                        .frame(maxWidth: .infinity)
                        .frame(height: 76, alignment: .top)

                        LazyVGrid(columns: Array(repeating: GridItem(.fixed(55), spacing: 5), count: 4), spacing: 6) {
                            OperatorCompactKnob(title: "Speed", value: binding("lfo.speed"), range: 0...99, defaultValue: EditParameterDefaults.values["lfo.speed"])
                            OperatorCompactKnob(title: "Delay", value: binding("lfo.delay"), range: 0...99, defaultValue: EditParameterDefaults.values["lfo.delay"])
                            OperatorCompactKnob(title: "PMD", value: binding("lfo.pmd"), range: 0...99, defaultValue: EditParameterDefaults.values["lfo.pmd"])
                            OperatorCompactKnob(title: "MOD PITCH", value: modPitchBinding, range: 0...99, defaultValue: 0)
                            OperatorCompactKnob(title: "AMD", value: binding("lfo.amd"), range: 0...99, defaultValue: EditParameterDefaults.values["lfo.amd"])
                            OperatorCompactKnob(title: "PMS", value: binding("lfo.pms"), range: 0...7, defaultValue: EditParameterDefaults.values["lfo.pms"])
                            OperatorCompactKnob(title: "AMS", value: binding("lfo.ams"), range: 0...3, defaultValue: EditParameterDefaults.values["lfo.ams"])
                            OperatorCompactKnob(title: "MOD AMP", value: modAmpBinding, range: 0...99, defaultValue: 0)
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .center)
                }
                .frame(width: EditLayout.splitPanelWidth)
                .frame(height: EditLayout.panelHeight)

                EditPanel(title: "PITCH EG") {
                    VStack(alignment: .center, spacing: 8) {
                        PitchEgGraph(values: values)
                            .frame(width: 226, height: 54)

                        LazyVGrid(columns: Array(repeating: GridItem(.fixed(74), spacing: 8), count: 3), spacing: 8) {
                            ForEach(["PR1", "PR2", "PR3"], id: \.self) { key in
                                EditKnob(title: key, value: binding(key.lowercased()), range: 0...99, defaultValue: EditParameterDefaults.values[key.lowercased()])
                            }
                            ForEach(["PL1", "PL2", "PL3"], id: \.self) { key in
                                EditKnob(title: key, value: binding(key.lowercased()), range: 0...99, defaultValue: EditParameterDefaults.values[key.lowercased()])
                            }
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .center)
                }
                .frame(width: EditLayout.splitPanelWidth)
                .frame(height: EditLayout.panelHeight)
            }
            .frame(width: EditLayout.totalPanelWidth, alignment: .topLeading)
            .frame(maxHeight: .infinity, alignment: .top)

            if showingWavePicker {
                Color.black.opacity(0.34)
                    .contentShape(Rectangle())
                    .onTapGesture {
                        showingWavePicker = false
                    }

                EditOptionPickerPanel(
                    title: "LFO WAVE",
                    options: waveNames,
                    selectedIndex: waveNames.firstIndex(of: lfoWave) ?? 0,
                    onSelect: { index in
                        if waveNames.indices.contains(index) {
                            selectWave(waveNames[index])
                        }
                        showingWavePicker = false
                    },
                    onClose: {
                        showingWavePicker = false
                    }
                )
                .frame(width: 260, height: 204)
                .zIndex(2)
            }
        }
        .frame(width: EditLayout.totalPanelWidth, alignment: .topLeading)
        .frame(maxHeight: .infinity, alignment: .top)
    }

    private func binding(_ key: String) -> Binding<Int> {
        Binding(
            get: { values[key] ?? 0 },
            set: {
                values[key] = $0
                synth.setEditValue(key, $0)
            }
        )
    }

    private var modPitchBinding: Binding<Int> {
        Binding(
            get: { synth.modWheelPitchRange },
            set: { synth.setModWheelPitchRange($0) }
        )
    }

    private var modAmpBinding: Binding<Int> {
        Binding(
            get: { synth.modWheelAmpRange },
            set: { synth.setModWheelAmpRange($0) }
        )
    }

    private var waveNames: [String] {
        ["SAW UP", "SQUARE", "TRIANGLE", "S/H"]
    }

    private func selectWave(_ wave: String) {
        lfoWave = wave
        let index = waveNames.firstIndex(of: wave) ?? 0
        values["lfo.wave"] = index
        synth.setEditValue("lfo.wave", index)
    }

}

private struct FxEditPage: View {
    @EnvironmentObject private var synth: MobileSynthModel
    @Binding var values: [String: Int]

    var body: some View {
        HStack(alignment: .top, spacing: 8) {
            EditPanel(title: "FX") {
                VStack(alignment: .leading, spacing: 10) {
                    Button(fxEnabled ? "FX ON" : "FX OFF") {
                        let enabled = !fxEnabled
                        values["fx.enabled"] = enabled ? 1 : 0
                        synth.setEffectsEnabled(enabled)
                    }
                    .buttonStyle(EditPanelButtonStyle(active: fxEnabled))
                    .frame(width: 86, height: 28)

                    LazyVGrid(columns: Array(repeating: GridItem(.fixed(74), spacing: 8), count: 3), spacing: 8) {
                        ForEach(fxParameters, id: \.title) { parameter in
                            EditKnob(title: parameter.title, value: binding(parameter.key), range: parameter.range, defaultValue: EditParameterDefaults.values[parameter.key])
                        }
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .frame(width: EditLayout.totalPanelWidth)
            .frame(height: EditLayout.panelHeight)
        }
        .frame(width: EditLayout.totalPanelWidth, alignment: .topLeading)
        .frame(maxHeight: .infinity, alignment: .top)
    }

    private func binding(_ key: String) -> Binding<Int> {
        Binding(
            get: { values[key] ?? 0 },
            set: {
                values[key] = $0
                synth.setEditValue(key, $0)
            }
        )
    }

    private var fxEnabled: Bool {
        (values["fx.enabled"] ?? 1) != 0
    }

    private var fxParameters: [EditParameter] {
        [
            .init(title: "Reverb", key: "fx.reverb", range: 0...99),
            .init(title: "Delay", key: "fx.delay", range: 0...99),
            .init(title: "Chorus", key: "fx.chorus", range: 0...99),
            .init(title: "RevMix", key: "fx.revmix", range: 0...99),
            .init(title: "DlyMix", key: "fx.dlymix", range: 0...99),
            .init(title: "Tone", key: "fx.tone", range: 0...99)
        ]
    }
}

private struct EditOptionPickerPanel: View {
    let title: String
    let options: [String]
    let selectedIndex: Int
    let onSelect: (Int) -> Void
    let onClose: () -> Void

    var body: some View {
        VStack(spacing: 6) {
            HStack {
                Text(title)
                    .font(.system(size: 15, weight: .heavy, design: .monospaced))
                    .foregroundStyle(EditSkin.textPrimary)

                Spacer()

                Button("CLOSE", action: onClose)
                    .buttonStyle(.plain)
                    .font(.system(size: 11, weight: .heavy, design: .monospaced))
                    .foregroundStyle(EditSkin.textMuted)
                    .frame(width: 58, height: 24)
                    .background(RoundedRectangle(cornerRadius: 3).fill(Color(hexValue: 0x2b2c27)))
                    .overlay(RoundedRectangle(cornerRadius: 3).stroke(Color.black.opacity(0.65), lineWidth: 1))
            }
            .padding(.horizontal, 8)
            .padding(.top, 7)

            ScrollView {
                VStack(spacing: 3) {
                    ForEach(options.indices, id: \.self) { index in
                        Button {
                            onSelect(index)
                        } label: {
                            HStack {
                                Text(options[index])
                                    .font(.system(size: 14, weight: .heavy, design: .monospaced))
                                    .foregroundStyle(index == selectedIndex ? Color(hexValue: 0x0b1c1a) : EditSkin.textPrimary)

                                Spacer()

                                if index == selectedIndex {
                                    Text("SELECTED")
                                        .font(.system(size: 9, weight: .heavy, design: .monospaced))
                                        .foregroundStyle(Color(hexValue: 0x0b1c1a).opacity(0.72))
                                }
                            }
                            .padding(.horizontal, 10)
                            .frame(height: 30)
                            .background(
                                RoundedRectangle(cornerRadius: 3)
                                    .fill(index == selectedIndex
                                          ? LinearGradient(colors: [Color(hexValue: 0x32e6c8), Color(hexValue: 0x15937f)], startPoint: .top, endPoint: .bottom)
                                          : LinearGradient(colors: [Color(hexValue: 0x171812), Color(hexValue: 0x0c0c09)], startPoint: .top, endPoint: .bottom))
                            )
                            .overlay(
                                RoundedRectangle(cornerRadius: 3)
                                    .stroke(index == selectedIndex ? Color(hexValue: 0x8df8ea).opacity(0.7) : Color(hexValue: 0x403728), lineWidth: 1)
                            )
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            .padding(.horizontal, 8)
            .padding(.bottom, 8)
        }
        .background(
            RoundedRectangle(cornerRadius: 5)
                .fill(LinearGradient(colors: [Color(hexValue: 0x27261f), Color(hexValue: 0x10100c)], startPoint: .top, endPoint: .bottom))
        )
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(Color(hexValue: 0x3edcca).opacity(0.68), lineWidth: 1.5))
        .shadow(color: Color.black.opacity(0.55), radius: 12, x: 0, y: 8)
    }
}

private struct SettingEditPage: View {
    @EnvironmentObject private var synth: MobileSynthModel
    @State private var activePicker: SettingPicker?

    var body: some View {
        ZStack {
            HStack(alignment: .top, spacing: EditLayout.panelGap) {
                EditPanel(title: "MIDI") {
                    VStack(alignment: .leading, spacing: 10) {
                        settingControl(
                            title: "INPUT",
                            value: selectedMIDIInputName,
                            action: { activePicker = .midiInput }
                        )

                        settingControl(
                            title: "CHANNEL",
                            value: midiChannelName,
                            action: { activePicker = .midiChannel }
                        )

                        Button("RESCAN MIDI") {
                            synth.refreshMIDISettings()
                        }
                        .buttonStyle(EditPanelButtonStyle(active: false))
                        .frame(width: 116, height: 28)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
                .frame(width: EditLayout.splitPanelWidth)
                .frame(height: EditLayout.panelHeight)

                EditPanel(title: "AUDIO") {
                    VStack(alignment: .leading, spacing: 10) {
                        settingValue(title: "OUTPUT", value: audioOutputName)

                        settingValue(title: "VERSION", value: appVersion)

                        HStack(spacing: 7) {
                            Text("ROUTE")
                                .font(.system(size: 12, weight: .bold))
                                .foregroundStyle(EditSkin.textMuted)
                                .frame(width: 70, alignment: .leading)

                            AudioRoutePickerButton()
                                .frame(maxWidth: .infinity)
                                .frame(height: 30)
                        }

                        settingControl(
                            title: "INPUT",
                            value: selectedAudioInputName,
                            action: { activePicker = .audioInput }
                        )

                        Button("REFRESH AUDIO") {
                            synth.refreshAudioDevices()
                        }
                        .buttonStyle(EditPanelButtonStyle(active: false))
                        .frame(width: 124, height: 28)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
                .frame(width: EditLayout.splitPanelWidth)
                .frame(height: EditLayout.panelHeight)
            }

            if let activePicker {
                Color.black.opacity(0.34)
                    .contentShape(Rectangle())
                    .onTapGesture {
                        self.activePicker = nil
                    }

                EditOptionPickerPanel(
                    title: activePicker.title,
                    options: options(for: activePicker),
                    selectedIndex: selectedIndex(for: activePicker),
                    onSelect: { index in
                        select(index, for: activePicker)
                        self.activePicker = nil
                    },
                    onClose: {
                        self.activePicker = nil
                    }
                )
                .frame(width: 300, height: pickerHeight(for: activePicker))
                .zIndex(2)
            }
        }
        .frame(width: EditLayout.totalPanelWidth, alignment: .topLeading)
        .frame(maxHeight: .infinity, alignment: .top)
        .onAppear {
            synth.refreshMIDISettings()
            synth.refreshAudioDevices()
        }
    }

    private func settingControl(title: String, value: String, action: @escaping () -> Void) -> some View {
        HStack(spacing: 7) {
            Text(title)
                .font(.system(size: 12, weight: .bold))
                .foregroundStyle(EditSkin.textMuted)
                .frame(width: 70, alignment: .leading)

            Button(action: action) {
                DropdownLook(text: value)
            }
            .buttonStyle(.plain)
            .frame(maxWidth: .infinity)
            .frame(height: 32)
        }
    }

    private func settingValue(title: String, value: String) -> some View {
        HStack(spacing: 7) {
            Text(title)
                .font(.system(size: 12, weight: .bold))
                .foregroundStyle(EditSkin.textMuted)
                .frame(width: 70, alignment: .leading)

            Text(value)
                .font(.system(size: 12, weight: .bold))
                .foregroundStyle(EditSkin.textPrimary)
                .lineLimit(1)
                .minimumScaleFactor(0.65)
                .frame(maxWidth: .infinity, minHeight: 28, maxHeight: 28, alignment: .leading)
                .padding(.horizontal, 8)
                .background(RoundedRectangle(cornerRadius: 3).fill(Color(hexValue: 0x070806)))
                .overlay(RoundedRectangle(cornerRadius: 3).stroke(EditSkin.panelBorder, lineWidth: 1))
        }
    }

    private var appVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "-"
    }

    private func options(for picker: SettingPicker) -> [String] {
        switch picker {
        case .midiInput:
            return ["ALL MIDI INPUTS"] + synth.midiInputDevices.map(\.name)
        case .midiChannel:
            return ["ALL"] + (1...16).map { "CH \($0)" }
        case .audioInput:
            return ["SYSTEM DEFAULT"] + synth.audioInputDevices.map(\.name)
        }
    }

    private func selectedIndex(for picker: SettingPicker) -> Int {
        switch picker {
        case .midiInput:
            guard let id = synth.selectedMIDIInputID,
                  let index = synth.midiInputDevices.firstIndex(where: { $0.id == id }) else {
                return 0
            }
            return index + 1
        case .midiChannel:
            return synth.midiReceiveChannel ?? 0
        case .audioInput:
            guard let id = synth.selectedAudioInputID,
                  let index = synth.audioInputDevices.firstIndex(where: { $0.id == id }) else {
                return 0
            }
            return index + 1
        }
    }

    private func select(_ index: Int, for picker: SettingPicker) {
        switch picker {
        case .midiInput:
            if index == 0 {
                synth.selectMIDIInput(id: nil)
            } else {
                let deviceIndex = index - 1
                guard synth.midiInputDevices.indices.contains(deviceIndex) else { return }
                synth.selectMIDIInput(id: synth.midiInputDevices[deviceIndex].id)
            }
        case .midiChannel:
            synth.setMIDIReceiveChannel(index == 0 ? nil : index)
        case .audioInput:
            if index == 0 {
                synth.selectAudioInput(id: nil)
            } else {
                let deviceIndex = index - 1
                guard synth.audioInputDevices.indices.contains(deviceIndex) else { return }
                synth.selectAudioInput(id: synth.audioInputDevices[deviceIndex].id)
            }
        }
    }

    private func pickerHeight(for picker: SettingPicker) -> CGFloat {
        min(306, CGFloat(options(for: picker).count) * 33 + 42)
    }

    private var selectedMIDIInputName: String {
        guard let id = synth.selectedMIDIInputID else { return "ALL MIDI INPUTS" }
        return synth.midiInputDevices.first { $0.id == id }?.name ?? "MISSING INPUT"
    }

    private var midiChannelName: String {
        guard let channel = synth.midiReceiveChannel else { return "ALL" }
        return "CH \(channel)"
    }

    private var audioOutputName: String {
        synth.audioOutputDevices.first?.name ?? "SYSTEM OUTPUT"
    }

    private var selectedAudioInputName: String {
        guard let id = synth.selectedAudioInputID else { return "SYSTEM DEFAULT" }
        return synth.audioInputDevices.first { $0.id == id }?.name ?? "SYSTEM DEFAULT"
    }

    private enum SettingPicker {
        case midiInput
        case midiChannel
        case audioInput

        var title: String {
            switch self {
            case .midiInput: return "MIDI INPUT"
            case .midiChannel: return "MIDI CHANNEL"
            case .audioInput: return "AUDIO INPUT"
            }
        }
    }
}

private struct AudioRoutePickerButton: UIViewRepresentable {
    func makeUIView(context: Context) -> AVRoutePickerView {
        let view = AVRoutePickerView()
        view.activeTintColor = UIColor(red: 0.15, green: 0.88, blue: 0.76, alpha: 1.0)
        view.tintColor = UIColor(red: 0.79, green: 0.82, blue: 0.78, alpha: 1.0)
        view.backgroundColor = UIColor.clear
        return view
    }

    func updateUIView(_ uiView: AVRoutePickerView, context: Context) {}
}

private struct OperatorEditPage: View {
    @EnvironmentObject private var synth: MobileSynthModel
    let index: Int
    @Binding var values: [String: Int]

    var body: some View {
        HStack(alignment: .top, spacing: 8) {
            EditPanel(title: "ALG") {
                VStack(spacing: 14) {
                    AlgorithmDiagram(algorithm: values["alg"] ?? 1, feedback: values["fb"] ?? 0, highlightedOperator: index + 1)
                        .frame(width: 122, height: 112)

                    HStack(spacing: 8) {
                        EditKnob(title: "ALG", value: globalBinding("alg"), range: 1...8, defaultValue: EditParameterDefaults.values["alg"])
                        EditKnob(title: "FB", value: globalBinding("fb"), range: 0...7, defaultValue: EditParameterDefaults.values["fb"])
                    }
                }
            }
            .frame(width: EditLayout.sidePanelWidth)
            .frame(height: EditLayout.panelHeight)

            VStack(spacing: 0) {
                HStack {
                    Button("OP \(index + 1)") {
                        let key = "op\(index + 1).enabled"
                        let enabled = (values[key] ?? 1) == 0
                        values[key] = enabled ? 1 : 0
                        synth.setOperatorEnabled(operatorIndex: index, enabled: enabled)
                    }
                    .buttonStyle(EditPanelButtonStyle(active: (values["op\(index + 1).enabled"] ?? 1) != 0))
                    .frame(width: 58, height: 31)

                    Button("AM") {
                        let key = "op\(index + 1).am"
                        let enabled = (values[key] ?? 1) == 0
                        values[key] = enabled ? 1 : 0
                        synth.setOperatorAmpModEnabled(operatorIndex: index, enabled: enabled)
                    }
                    .buttonStyle(EditPanelButtonStyle(active: (values["op\(index + 1).am"] ?? 1) != 0, activeColor: .blue))
                    .frame(width: 58, height: 31)

                    Spacer()

                    Text(index == 0 ? "Carrier" : "Modulator")
                        .font(.system(size: 18, weight: .bold))
                        .foregroundStyle(EditSkin.textMuted)
                }
                .padding(.horizontal, 6)
                .padding(.top, 4)

                HStack {
                    Text("AR")
                    Spacer()
                    Text("D1")
                    Spacer()
                    Text("D2")
                    Spacer()
                    Text("RR")
                }
                .font(.system(size: 14, weight: .bold))
                .foregroundStyle(EditSkin.textMuted)
                .padding(.horizontal, 18)
                .padding(.top, 3)

                OperatorEnvelopeGraph(operatorIndex: index, values: values)
                    .frame(height: 74)
                    .padding(.horizontal, 6)
                    .padding(.top, 2)

                HStack(spacing: 5) {
                    ForEach(envelopeParameters, id: \.title) { parameter in
                        OperatorCompactKnob(title: parameter.title, value: binding(parameter.key), range: parameter.range, defaultValue: EditParameterDefaults.values[parameter.key])
                    }
                }
                .padding(.top, 6)

                HStack(spacing: 5) {
                    ForEach(["Ratio", "Detune", "Level", "RateSc", "LevelSc", "Vel"], id: \.self) { title in
                        OperatorCompactKnob(
                            title: title,
                            value: binding(key(for: title)),
                            range: range(for: title),
                            defaultValue: EditParameterDefaults.values[key(for: title)],
                            valueFormatter: valueFormatter(for: title)
                        )
                    }
                }
                .padding(.top, 4)
            }
            .padding(4)
            .frame(width: EditLayout.mainPanelWidth)
            .frame(height: EditLayout.panelHeight, alignment: .top)
            .background(
                RoundedRectangle(cornerRadius: 3)
                    .fill(LinearGradient(colors: [EditSkin.panelTop, EditSkin.panelBottom], startPoint: .top, endPoint: .bottom))
            )
            .overlay(RoundedRectangle(cornerRadius: 3).stroke(EditSkin.panelBorder, lineWidth: 1))
        }
        .frame(width: EditLayout.totalPanelWidth, alignment: .topLeading)
        .frame(maxHeight: .infinity, alignment: .top)
    }

    private func range(for title: String) -> ClosedRange<Int> {
        switch title {
        case "Ratio": return 0...63
        case "Detune": return -3...3
        case "RateSc": return 0...3
        case "LevelSc": return 0...99
        case "Vel": return 0...7
        default: return 0...99
        }
    }

    private func key(for title: String) -> String {
        switch title {
        case "RateSc": return "ratesc"
        case "LevelSc": return "levelsc"
        default: return title.lowercased()
        }
    }

    private func valueFormatter(for title: String) -> ((Int) -> String)? {
        guard title == "Ratio" else { return nil }
        return { synth.operatorRatioText(for: $0) }
    }

    private var envelopeParameters: [EditParameter] {
        [
            .init(title: "AR", key: "ar", range: 0...31),
            .init(title: "D1R", key: "d1r", range: 0...31),
            .init(title: "D1L", key: "d1l", range: 0...15),
            .init(title: "D2R", key: "d2r", range: 0...31),
            .init(title: "RR", key: "rr", range: 0...15)
        ]
    }

    private func binding(_ key: String) -> Binding<Int> {
        let opKey = "op\(index + 1).\(key)"
        return Binding(
            get: { values[opKey] ?? EditParameterDefaults.values[key] ?? 0 },
            set: {
                values[opKey] = $0
                synth.setOperatorEditValue(operatorIndex: index, key: key, value: $0)
            }
        )
    }

    private func globalBinding(_ key: String) -> Binding<Int> {
        Binding(
            get: { values[key] ?? EditParameterDefaults.values[key] ?? 0 },
            set: {
                values[key] = $0
                synth.setEditValue(key, $0)
            }
        )
    }
}

private struct EditParameter {
    let title: String
    let key: String
    let range: ClosedRange<Int>
}

private enum EditLayout {
    static let tabWidth: CGFloat = 118
    static let sidePanelWidth: CGFloat = 170
    static let mainPanelWidth: CGFloat = 364
    static let panelGap: CGFloat = 8
    static let totalPanelWidth: CGFloat = sidePanelWidth + panelGap + mainPanelWidth
    static let splitPanelWidth: CGFloat = (totalPanelWidth - panelGap) / 2
    static let panelHeight: CGFloat = 304
}

private struct EditBackPanel: View {
    var body: some View {
        RoundedRectangle(cornerRadius: 3)
            .fill(LinearGradient(colors: [EditSkin.panelTop, EditSkin.panelBottom], startPoint: .top, endPoint: .bottom))
            .overlay(RoundedRectangle(cornerRadius: 3).stroke(EditSkin.panelBorder, lineWidth: 1))
            .shadow(color: .black.opacity(0.45), radius: 2, x: 0, y: 2)
    }
}

private struct EditPanel<Content: View>: View {
    let title: String
    var contentPadding: CGFloat = 8
    @ViewBuilder var content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 9) {
            Text(title)
                .font(.system(size: 13, weight: .bold))
                .foregroundStyle(EditSkin.textPrimary)
                .frame(maxWidth: .infinity, alignment: .center)

            content
        }
        .padding(contentPadding)
        .frame(maxWidth: .infinity, minHeight: EditLayout.panelHeight, maxHeight: EditLayout.panelHeight, alignment: .top)
        .background(
            RoundedRectangle(cornerRadius: 3)
                .fill(LinearGradient(colors: [EditSkin.panelTop, EditSkin.panelBottom], startPoint: .top, endPoint: .bottom))
        )
        .overlay(RoundedRectangle(cornerRadius: 3).stroke(EditSkin.panelBorder, lineWidth: 1))
    }
}

private struct EditKnob: View {
    let title: String
    @Binding var value: Int
    let range: ClosedRange<Int>
    var defaultValue: Int? = nil

    var body: some View {
        VStack(spacing: 2) {
            Text(title)
                .font(.system(size: 13, weight: .bold))
                .foregroundStyle(EditSkin.textMuted)
                .lineLimit(1)
                .minimumScaleFactor(0.58)
                .frame(height: 16)

            MiniRotaryKnob(value: $value, range: range, defaultValue: defaultValue)
                .frame(width: 40, height: 40)

            Text("\(value)")
                .font(.system(size: 14, weight: .bold, design: .monospaced))
                .foregroundStyle(EditSkin.valueText)
                .frame(width: 58, height: 18)
                .background(Rectangle().fill(Color(hexValue: 0x050504)))
                .overlay(Rectangle().stroke(Color(hexValue: 0x4b3e23), lineWidth: 1))
        }
        .frame(width: 68, height: 72)
    }
}

private struct OperatorCompactKnob: View {
    let title: String
    @Binding var value: Int
    let range: ClosedRange<Int>
    var defaultValue: Int? = nil
    var valueFormatter: ((Int) -> String)? = nil

    var body: some View {
        VStack(spacing: 2) {
            Text(title)
                .font(.system(size: 13, weight: .bold))
                .foregroundStyle(EditSkin.textMuted)
                .lineLimit(1)
                .minimumScaleFactor(0.58)
                .frame(height: 16)

            MiniRotaryKnob(value: $value, range: range, defaultValue: defaultValue)
                .frame(width: 38, height: 38)

            Text(valueFormatter?(value) ?? String(value))
                .font(.system(size: 16, weight: .bold, design: .monospaced))
                .foregroundStyle(EditSkin.valueText)
                .frame(width: 52, height: 20)
                .background(Rectangle().fill(Color(hexValue: 0x050504)))
                .overlay(Rectangle().stroke(Color(hexValue: 0x4b3e23), lineWidth: 1))
        }
        .frame(width: 55, height: 76)
    }
}

private struct MiniRotaryKnob: View {
    @Binding var value: Int
    let range: ClosedRange<Int>
    let defaultValue: Int?
    @State private var dragStartValue: Int?
    @State private var dragStartLocation: CGPoint?

    var body: some View {
        GeometryReader { proxy in
            let normalized = CGFloat(value - range.lowerBound) / CGFloat(max(1, range.upperBound - range.lowerBound))
            Canvas { context, size in
                draw(context: &context, size: size, normalized: normalized)
            }
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { gesture in
                        if dragStartValue == nil {
                            dragStartValue = value
                            dragStartLocation = gesture.location
                        }

                        let startValue = dragStartValue ?? value
                        let startLocation = dragStartLocation ?? gesture.startLocation
                        let horizontalEscape = abs(gesture.location.x - startLocation.x)
                        let sensitivity: CGFloat = horizontalEscape >= 22 ? 0.20 : 1.0
                        let deltaY = startLocation.y - gesture.location.y
                        let span = CGFloat(max(1, range.upperBound - range.lowerBound))
                        let raw = CGFloat(startValue) + (deltaY / max(1, proxy.size.height)) * span * sensitivity
                        value = max(range.lowerBound, min(range.upperBound, Int(raw.rounded())))
                    }
                    .onEnded { _ in
                        dragStartValue = nil
                        dragStartLocation = nil
                    }
            )
            .simultaneousGesture(
                TapGesture(count: 2)
                    .onEnded {
                        value = clampedDefaultValue
                    }
            )
        }
    }

    private var clampedDefaultValue: Int {
        let fallback = range.contains(0) ? 0 : range.lowerBound
        return max(range.lowerBound, min(range.upperBound, defaultValue ?? fallback))
    }

    private func draw(context: inout GraphicsContext, size: CGSize, normalized: CGFloat) {
        let center = CGPoint(x: size.width / 2, y: size.height / 2)
        let radius = min(size.width, size.height) * 0.42

        for index in 0..<31 {
            let ratio = Double(index) / 30
            let angle = Double.pi * (135 + 270 * ratio) / 180
            let dot = CGPoint(x: center.x + cos(angle) * radius, y: center.y + sin(angle) * radius)
            let color = ratio <= Double(normalized) ? EditSkin.teal : Color(hexValue: 0x4c6b68).opacity(0.65)
            context.fill(Path(ellipseIn: CGRect(x: dot.x - 1.05, y: dot.y - 1.05, width: 2.1, height: 2.1)), with: .color(color))
        }

        let knob = CGRect(x: center.x - radius * 0.58, y: center.y - radius * 0.58, width: radius * 1.16, height: radius * 1.16)
        context.fill(Path(ellipseIn: knob.offsetBy(dx: 0, dy: 2)), with: .color(Color.black.opacity(0.55)))
        context.fill(Path(ellipseIn: knob), with: .linearGradient(
            Gradient(colors: [Color(hexValue: 0x333532), Color(hexValue: 0x060706)]),
            startPoint: CGPoint(x: knob.midX, y: knob.minY),
            endPoint: CGPoint(x: knob.midX, y: knob.maxY)
        ))
        context.stroke(Path(ellipseIn: knob), with: .color(Color.black.opacity(0.9)), lineWidth: 1.2)

        let pointerAngle = CGFloat((135 + 270 * Double(normalized)) * Double.pi / 180)
        let pointer = Path { path in
            path.move(to: center)
            path.addLine(to: CGPoint(x: center.x + cos(pointerAngle) * radius * 0.44,
                                     y: center.y + sin(pointerAngle) * radius * 0.44))
        }
        context.stroke(pointer, with: .color(Color(hexValue: 0xc7c3ac)), lineWidth: 2.3)
    }
}

private struct AlgorithmDiagram: View {
    let algorithm: Int
    let feedback: Int
    var highlightedOperator: Int?

    var body: some View {
        Canvas { context, size in
            let side = min(size.width, size.height) - 2
            let area = CGRect(
                x: (size.width - side) * 0.5,
                y: (size.height - side) * 0.5,
                width: side,
                height: side
            )
            context.fill(Path(area), with: .linearGradient(
                Gradient(colors: [Color(hexValue: 0x29261f), Color(hexValue: 0x14130f)]),
                startPoint: CGPoint(x: area.midX, y: area.minY),
                endPoint: CGPoint(x: area.midX, y: area.maxY)
            ))
            context.stroke(Path(area), with: .color(EditSkin.valueText.opacity(0.85)), lineWidth: 1)

            let layout = AlgorithmLayout(algorithm: max(1, min(8, algorithm)), area: area)
            let boxSize = max(CGFloat(8), area.width * 0.145)
            let feedbackSegments = feedback > 0 ? algorithmFeedbackSegments(op4: layout.points[3], boxSize: boxSize, area: area) : []

            var illustrationBounds = boundsFor(points: layout.points, boxSize: boxSize)
            illustrationBounds = illustrationBounds.union(boundsFor(segments: layout.segments + feedbackSegments))
            let targetArea = area.insetBy(dx: area.width * 0.08, dy: area.height * 0.08)
            let offset = CGPoint(x: targetArea.midX - illustrationBounds.midX, y: targetArea.midY - illustrationBounds.midY)

            var lines = Path()
            for segment in layout.segments {
                lines.move(to: segment.start.translated(offset))
                lines.addLine(to: segment.end.translated(offset))
            }
            context.stroke(lines, with: .color(EditSkin.valueText.opacity(0.85)), lineWidth: 1.6)

            if !feedbackSegments.isEmpty {
                var feedbackPath = Path()
                for (index, segment) in feedbackSegments.enumerated() {
                    if index == 0 {
                        feedbackPath.move(to: segment.start.translated(offset))
                    }
                    feedbackPath.addLine(to: segment.end.translated(offset))
                }
                context.stroke(feedbackPath, with: .color(EditSkin.valueText.opacity(0.85)), lineWidth: 1.4)
            }

            for (offsetIndex, point) in layout.points.enumerated() {
                let operatorNumber = offsetIndex + 1
                let isHighlighted = highlightedOperator == operatorNumber
                let center = point.translated(offset)
                let box = CGRect(x: center.x - boxSize * 0.5, y: center.y - boxSize * 0.5, width: boxSize, height: boxSize)
                context.fill(Path(box), with: .color(isHighlighted ? EditSkin.buttonActive : Color(hexValue: 0xf1dfaa)))
                if isHighlighted {
                    context.stroke(Path(box.insetBy(dx: -1.2, dy: -1.2)), with: .color(EditSkin.teal), lineWidth: 1.6)
                }
                context.draw(Text("\(operatorNumber)").font(.system(size: max(6.5, boxSize * 0.62), weight: .bold)).foregroundColor(isHighlighted ? Color.white : Color(hexValue: 0x11100d)),
                             at: CGPoint(x: box.midX, y: box.midY))
            }

            context.draw(Text("\(max(1, min(8, algorithm)))").font(.system(size: max(7, area.width * 0.10), weight: .bold)).foregroundColor(EditSkin.valueText),
                         at: CGPoint(x: area.minX + 10, y: area.maxY - 11))
        }
    }
}

private struct PitchEgGraph: View {
    let values: [String: Int]

    var body: some View {
        Canvas { context, size in
            let area = CGRect(origin: .zero, size: size).insetBy(dx: 1, dy: 1)
            context.fill(Path(roundedRect: area, cornerRadius: 4), with: .color(Color(hexValue: 0x050706)))
            context.stroke(Path(roundedRect: area, cornerRadius: 4), with: .color(Color(hexValue: 0x2a342f)), lineWidth: 1)

            let graph = area.insetBy(dx: 4, dy: 3)
            var mid = Path()
            mid.move(to: CGPoint(x: graph.minX, y: graph.midY))
            mid.addLine(to: CGPoint(x: graph.maxX, y: graph.midY))
            context.stroke(mid, with: .color(Color(hexValue: 0x2a342f).opacity(0.75)), lineWidth: 1)

            let w1 = pegRateWeight(values["pr1"] ?? 99)
            let w2 = pegRateWeight(values["pr2"] ?? 99)
            let w3 = pegRateWeight(values["pr3"] ?? 99)
            let total = max(0.001, w1 + w2 + w3)
            let x0 = graph.minX
            let x1 = graph.minX + graph.width * w1 / total
            let x2 = x1 + graph.width * w2 / total
            let x3 = graph.maxX
            let points = [
                CGPoint(x: x0, y: pitchEgY(level: values["pl3"] ?? 50, graph: graph)),
                CGPoint(x: x1, y: pitchEgY(level: values["pl1"] ?? 50, graph: graph)),
                CGPoint(x: x2, y: pitchEgY(level: values["pl2"] ?? 50, graph: graph)),
                CGPoint(x: x3, y: pitchEgY(level: values["pl3"] ?? 50, graph: graph))
            ]

            for x in [x1, x2] {
                var marker = Path()
                marker.move(to: CGPoint(x: x, y: graph.minY))
                marker.addLine(to: CGPoint(x: x, y: graph.maxY))
                context.stroke(marker, with: .color(Color(hexValue: 0x2a342f)), lineWidth: 1)
            }

            drawPolyline(context: &context, points: points, color: Color(hexValue: 0xf1dfaa))
            drawScaleLabels(context: &context, rect: area, top: "+4", bottom: "-4")
        }
    }

    private func pitchEgY(level: Int, graph: CGRect) -> CGFloat {
        let normalized = max(-1, min(1, pegLevelToCents(level) / 4800))
        return graph.midY - normalized * graph.height * 0.46
    }
}

private struct AlgorithmSegment {
    let start: CGPoint
    let end: CGPoint
}

private struct AlgorithmLayout {
    let points: [CGPoint]
    let segments: [AlgorithmSegment]

    init(algorithm: Int, area: CGRect) {
        func px(_ x: CGFloat) -> CGFloat { area.minX + area.width * x / 100 }
        func py(_ y: CGFloat) -> CGFloat { area.minY + area.height * y / 100 }
        func point(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: px(x), y: py(y)) }
        func line(_ x1: CGFloat, _ y1: CGFloat, _ x2: CGFloat, _ y2: CGFloat) -> AlgorithmSegment {
            AlgorithmSegment(start: point(x1, y1), end: point(x2, y2))
        }

        switch algorithm {
        case 1:
            points = [point(50, 72), point(50, 56), point(50, 40), point(50, 24)]
            segments = [line(50, 18, 50, 82)]
        case 2:
            points = [point(50, 70), point(50, 52), point(30, 34), point(50, 34)]
            segments = [line(50, 30, 50, 82), line(30, 34, 50, 52)]
        case 3:
            points = [point(50, 70), point(50, 52), point(50, 34), point(70, 52)]
            segments = [line(50, 30, 50, 82), line(70, 52, 50, 70)]
        case 4:
            points = [point(30, 70), point(30, 52), point(50, 52), point(50, 34)]
            segments = [line(30, 52, 30, 82), line(50, 34, 50, 52), line(50, 52, 30, 70)]
        case 5:
            points = [point(30, 50), point(30, 31), point(50, 50), point(50, 31)]
            segments = [line(30, 31, 30, 66), line(30, 66, 50, 66), line(50, 31, 50, 66)]
        case 6:
            points = [point(30, 66), point(50, 66), point(70, 66), point(50, 36)]
            segments = [
                line(30, 66, 30, 80),
                line(30, 80, 70, 80),
                line(50, 36, 30, 66),
                line(50, 36, 50, 80),
                line(50, 36, 70, 66),
                line(70, 66, 70, 80)
            ]
        case 7:
            points = [point(30, 58), point(50, 58), point(70, 58), point(70, 35)]
            segments = [
                line(30, 58, 30, 76),
                line(30, 76, 70, 76),
                line(50, 58, 50, 76),
                line(70, 35, 70, 76)
            ]
        default:
            points = [point(20, 50), point(40, 50), point(60, 50), point(80, 50)]
            segments = [
                line(20, 50, 20, 76),
                line(20, 76, 80, 76),
                line(40, 50, 40, 76),
                line(60, 50, 60, 76),
                line(80, 50, 80, 76)
            ]
        }
    }
}

private func algorithmFeedbackSegments(op4: CGPoint, boxSize: CGFloat, area: CGRect) -> [AlgorithmSegment] {
    let loopInset = area.width * 0.14
    let topY = op4.y - boxSize * 0.5 - area.height * 0.07
    let rightX = op4.x + loopInset
    let lower = CGPoint(x: op4.x, y: op4.y + boxSize * 0.5)
    let lowerRight = CGPoint(x: rightX, y: lower.y)
    let upperRight = CGPoint(x: rightX, y: topY)
    let upper = CGPoint(x: op4.x, y: topY)
    let top = CGPoint(x: op4.x, y: op4.y - boxSize * 0.5)
    return [
        AlgorithmSegment(start: lower, end: lowerRight),
        AlgorithmSegment(start: lowerRight, end: upperRight),
        AlgorithmSegment(start: upperRight, end: upper),
        AlgorithmSegment(start: upper, end: top)
    ]
}

private func boundsFor(points: [CGPoint], boxSize: CGFloat) -> CGRect {
    var bounds = CGRect.null
    for point in points {
        bounds = bounds.union(CGRect(x: point.x - boxSize * 0.5, y: point.y - boxSize * 0.5, width: boxSize, height: boxSize))
    }
    return bounds
}

private func boundsFor(segments: [AlgorithmSegment]) -> CGRect {
    var bounds = CGRect.null
    for segment in segments {
        bounds = bounds.union(CGRect(
            x: min(segment.start.x, segment.end.x),
            y: min(segment.start.y, segment.end.y),
            width: abs(segment.start.x - segment.end.x),
            height: abs(segment.start.y - segment.end.y)
        ))
    }
    return bounds
}

private extension CGPoint {
    func translated(_ offset: CGPoint) -> CGPoint {
        CGPoint(x: x + offset.x, y: y + offset.y)
    }
}

private func pegLevelToCents(_ level: Int) -> CGFloat {
    let value = max(0, min(99, level))
    if value >= 50 {
        return CGFloat(value - 50) * 4800 / 49
    }
    return CGFloat(value - 50) * 4800 / 50
}

private func pegRateWeight(_ rate: Int) -> CGFloat {
    let normalized = CGFloat(max(0, min(99, rate))) / 99
    return 0.12 + (1 - normalized) * (1 - normalized) * 0.88
}

private struct OperatorEnvelopeGraph: View {
    let operatorIndex: Int
    let values: [String: Int]

    var body: some View {
        Canvas { context, size in
            let rect = CGRect(origin: .zero, size: size).insetBy(dx: 0, dy: 0)
            drawGraphBackground(context: &context, rect: rect)
            let prefix = "op\(operatorIndex + 1)."
            let ar = max(0, min(31, values[prefix + "ar"] ?? 20))
            let d1r = max(0, min(31, values[prefix + "d1r"] ?? 9))
            let d1l = max(0, min(15, values[prefix + "d1l"] ?? 12))
            let d2r = max(0, min(31, values[prefix + "d2r"] ?? 1))
            let rr = max(0, min(15, values[prefix + "rr"] ?? 5))
            let attackWeight = egTimeWeight(ar, max: 31)
            let decay1Weight = egTimeWeight(d1r, max: 31)
            let decay2Weight = egTimeWeight(d2r, max: 31)
            let releaseWeight = egTimeWeight(rr, max: 15)
            let total = max(0.001, attackWeight + decay1Weight + decay2Weight + releaseWeight)
            let left = rect.minX + 10
            let right = rect.maxX - 10
            let top = rect.minY + 10
            let bottom = rect.maxY - 10
            let width = right - left
            let x1 = left + width * attackWeight / total
            let x2 = x1 + width * decay1Weight / total
            let x3 = x2 + width * decay2Weight / total
            let sustainDb = d1l >= 15 ? CGFloat(96) : CGFloat(15 - d1l) * 3
            let y2 = top + min(max(sustainDb / 96, 0), 1) * (bottom - top)
            let y3 = d2r > 0 ? bottom : y2
            let points = [
                CGPoint(x: left, y: bottom),
                CGPoint(x: x1, y: top),
                CGPoint(x: x2, y: y2),
                CGPoint(x: x3, y: y3),
                CGPoint(x: right, y: bottom)
            ]
            for x in [x1, x2, x3] {
                var marker = Path()
                marker.move(to: CGPoint(x: x, y: top))
                marker.addLine(to: CGPoint(x: x, y: bottom))
                context.stroke(marker, with: .color(Color(hexValue: 0x38433d).opacity(0.72)), lineWidth: 1)
            }
            drawPolyline(context: &context, points: points, color: Color(hexValue: 0xf1dfaa))
        }
    }

    private func egTimeWeight(_ value: Int, max: Int) -> CGFloat {
        let normalized = 1 - CGFloat(value) / CGFloat(max)
        return 0.12 + normalized * normalized * 0.88
    }
}

private func drawGraphBackground(context: inout GraphicsContext, rect: CGRect) {
    context.fill(Path(roundedRect: rect, cornerRadius: 4), with: .color(Color(hexValue: 0x050706)))
    context.stroke(Path(roundedRect: rect, cornerRadius: 4), with: .color(Color(hexValue: 0x2a342f)), lineWidth: 1)
    for index in 1...3 {
        let x = rect.minX + rect.width * CGFloat(index) / 4
        var line = Path()
        line.move(to: CGPoint(x: x, y: rect.minY + 8))
        line.addLine(to: CGPoint(x: x, y: rect.maxY - 8))
        context.stroke(line, with: .color(Color(hexValue: 0x38433d).opacity(0.65)), lineWidth: 1)
    }
    var mid = Path()
    mid.move(to: CGPoint(x: rect.minX + 8, y: rect.midY))
    mid.addLine(to: CGPoint(x: rect.maxX - 8, y: rect.midY))
    context.stroke(mid, with: .color(Color(hexValue: 0x38433d).opacity(0.65)), lineWidth: 1)
}

private func drawPolyline(context: inout GraphicsContext, points: [CGPoint], color: Color) {
    guard let first = points.first else { return }
    var path = Path()
    path.move(to: first)
    for point in points.dropFirst() {
        path.addLine(to: point)
    }
    context.stroke(path, with: .color(color), lineWidth: 2.2)
}

private func drawScaleLabels(context: inout GraphicsContext, rect: CGRect, top: String, bottom: String) {
    context.draw(Text(top).font(.system(size: 10, weight: .bold)).foregroundColor(EditSkin.textMuted),
                 at: CGPoint(x: rect.minX + 14, y: rect.minY + 12))
    context.draw(Text(bottom).font(.system(size: 10, weight: .bold)).foregroundColor(EditSkin.textMuted),
                 at: CGPoint(x: rect.minX + 14, y: rect.maxY - 12))
}

private struct DropdownLook: View {
    let text: String

    var body: some View {
        HStack {
            Text(text)
                .font(.system(size: 14, weight: .bold))
                .lineLimit(1)
            Spacer()
            Image(systemName: "chevron.down")
        }
        .foregroundStyle(EditSkin.textPrimary)
        .padding(.horizontal, 9)
        .padding(.vertical, 4)
        .frame(maxWidth: .infinity, minHeight: 34)
        .background(RoundedRectangle(cornerRadius: 3).fill(Color(hexValue: 0x11100d)))
        .overlay(RoundedRectangle(cornerRadius: 3).stroke(Color(hexValue: 0x403728), lineWidth: 1))
    }
}

private struct SyncToggle: View {
    @Binding var isOn: Bool

    var body: some View {
        Button {
            isOn.toggle()
        } label: {
            RoundedRectangle(cornerRadius: 3)
                .fill(isOn ? EditSkin.teal.opacity(0.35) : Color(hexValue: 0x151511))
                .overlay(RoundedRectangle(cornerRadius: 3).stroke(EditSkin.panelBorder, lineWidth: 1))
                .overlay {
                    if isOn {
                        Image(systemName: "checkmark")
                            .font(.system(size: 12, weight: .bold))
                            .foregroundStyle(EditSkin.teal)
                    }
                }
                .frame(width: 24, height: 24)
        }
        .buttonStyle(.plain)
    }
}

private struct EditPanelButtonStyle: ButtonStyle {
    let active: Bool
    var activeColor: ActiveButtonColor = .green

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 12, weight: .bold))
            .foregroundStyle(active ? Color.white : EditSkin.textMuted)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(
                RoundedRectangle(cornerRadius: 3)
                    .fill(active ? activeFill : inactiveFill)
            )
            .overlay(RoundedRectangle(cornerRadius: 3).stroke(active ? activeStroke : Color.black.opacity(0.35), lineWidth: 1))
            .opacity(configuration.isPressed ? 0.75 : 1)
    }

    private var activeFill: LinearGradient {
        switch activeColor {
        case .green:
            return LinearGradient(colors: [Color(hexValue: 0x1fa08b), EditSkin.buttonActive], startPoint: .top, endPoint: .bottom)
        case .blue:
            return LinearGradient(colors: [Color(hexValue: 0x347faa), Color(hexValue: 0x17465f)], startPoint: .top, endPoint: .bottom)
        }
    }

    private var inactiveFill: LinearGradient {
        LinearGradient(colors: [Color(hexValue: 0x34352f), Color(hexValue: 0x252620)], startPoint: .top, endPoint: .bottom)
    }

    private var activeStroke: Color {
        switch activeColor {
        case .green:
            return EditSkin.teal.opacity(0.65)
        case .blue:
            return Color(hexValue: 0x58b7e8).opacity(0.72)
        }
    }
}

private enum ActiveButtonColor {
    case green
    case blue
}

private enum EditSkin {
    static let appBackground = Color(hexValue: 0x020405)
    static let panelTop = Color(hexValue: 0x2c2a21)
    static let panelBottom = Color(hexValue: 0x151610)
    static let panelBorder = Color(hexValue: 0x6a614d).opacity(0.50)
    static let textPrimary = Color(hexValue: 0xedf4f3)
    static let textMuted = Color(hexValue: 0xb7c2bd)
    static let valueText = Color(hexValue: 0xffff2b)
    static let teal = Color(hexValue: 0x25d9c4)
    static let lcdOn = Color(hexValue: 0x94e7ff)
    static let buttonActive = Color(hexValue: 0x16816d)
}

private enum EditParameterDefaults {
    static let values: [String: Int] = [
        "alg": 3, "fb": 6,
        "pr1": 99, "pr2": 99, "pr3": 99,
        "pl1": 50, "pl2": 50, "pl3": 50,
        "lfo.speed": 35, "lfo.delay": 0, "lfo.pmd": 0, "lfo.amd": 0, "lfo.pms": 0, "lfo.ams": 0,
        "fx.enabled": 1, "fx.reverb": 0, "fx.delay": 0, "fx.chorus": 0, "fx.revmix": 0, "fx.dlymix": 0, "fx.tone": 50,
        "ar": 20, "d1r": 9, "d1l": 12, "d2r": 1, "rr": 5,
        "ratio": 4, "detune": 0, "level": 99, "ratesc": 2, "levelsc": 1, "vel": 0
    ]
}

private extension Color {
    init(hexValue: UInt32) {
        self.init(
            red: Double((hexValue >> 16) & 0xff) / 255.0,
            green: Double((hexValue >> 8) & 0xff) / 255.0,
            blue: Double(hexValue & 0xff) / 255.0
        )
    }
}
