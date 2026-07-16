import SwiftUI
import UIKit
import UniformTypeIdentifiers

struct PlayView: View {
    @EnvironmentObject private var synth: MobileSynthModel
    @State private var importTarget: ImportTarget?
    @State private var importingFile = false
    @State private var exportingData = false
    @State private var exportFileURL: URL?
    @State private var keyboardBaseNote = 48
    @State private var keyboardScrollWhiteIndex: CGFloat = 16
    @State private var activeVoicePicker: VoicePickerTarget?
    @State private var showingBankPicker = false
    @State private var showingModePicker = false
    @State private var screenSafeAreaInsets = UIEdgeInsets.zero
    private let keyboardVisibleWhiteKeyCount = 17

    var body: some View {
        GeometryReader { proxy in
            let verticalInset: CGFloat = 6
            let panelGap: CGFloat = 2
            let contentWidth = max(1, proxy.size.width)
            let contentHeight = max(1, proxy.size.height - verticalInset * 2)
            let keyboardWidth = proxy.size.width
            let lowerHeight = max(118, contentHeight * 0.5 - 8)
            let topHeight = max(1, contentHeight - lowerHeight - panelGap)

            ZStack {
                VStack(spacing: panelGap) {
                    mainPanel
                        .frame(width: contentWidth, height: topHeight)
                        .ignoresSafeArea(.container, edges: .horizontal)

                    keyboardPanel(width: keyboardWidth, height: lowerHeight)
                        .ignoresSafeArea(.container, edges: .horizontal)
                }
                .padding(.vertical, verticalInset)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .center)

                if let activeVoicePicker {
                    voicePickerOverlay(target: activeVoicePicker, size: proxy.size)
                }

                if showingBankPicker {
                    bankPickerOverlay(size: proxy.size)
                }

                if showingModePicker {
                    modePickerOverlay(size: proxy.size)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .center)
            .background(MacSkin.appBackground)
        }
        .background(MacSkin.appBackground)
        .onAppear {
            _ = opalineDocumentsDirectory
            DispatchQueue.main.async {
                screenSafeAreaInsets = currentWindowSafeAreaInsets
            }
        }
        .sheet(isPresented: $importingFile) {
            OpalineDocumentPicker(
                mode: .importing(importContentTypes),
                initialDirectoryURL: opalineDocumentsDirectory
            ) { url in
                if let url {
                    handleImport(.success(url))
                } else {
                    importTarget = nil
                    importingFile = false
                }
            }
        }
        .sheet(isPresented: $exportingData) {
            if let exportFileURL {
                OpalineDocumentPicker(
                    mode: .exporting(exportFileURL),
                    initialDirectoryURL: opalineDocumentsDirectory
                ) { savedURL in
                    finishExport(savedURL: savedURL)
                }
            }
        }
    }

    private var currentWindowSafeAreaInsets: UIEdgeInsets {
        UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .flatMap(\.windows)
            .first(where: \.isKeyWindow)?
            .safeAreaInsets ?? .zero
    }

    private var mainPanel: some View {
        HStack(alignment: .top, spacing: 12) {
            keyboardShiftControls(direction: .down)
                .frame(width: 58)
                .frame(maxHeight: .infinity)

            VStack(spacing: 6) {
                playEditSwitch
                    .frame(height: 28)
                    .offset(y: 2)

                wheelPanel
                    .frame(maxHeight: .infinity)
                    .offset(y: 4)
            }
                .frame(width: 190)
                .frame(maxHeight: .infinity)

            rightPanel
                .frame(maxWidth: .infinity, maxHeight: .infinity)

            knobPanel
                .frame(width: 124)
                .frame(maxHeight: .infinity, alignment: .top)

            keyboardShiftControls(direction: .up)
                .frame(width: 58)
                .frame(maxHeight: .infinity)
        }
        .padding(8)
        .background(MetalPanel())
        .clipped()
    }

    private func keyboardShiftControls(direction: KeyboardShiftDirection) -> some View {
        VStack(spacing: 4) {
            switch direction {
            case .down:
                PanelButton(
                    "-1",
                    color: .dark,
                    disabled: previousWhiteKeyboardBaseNote(from: keyboardBaseNote) == keyboardBaseNote
                ) {
                    shiftKeyboardByWhiteKey(-1)
                }
                .frame(height: 48)

                PanelButton(
                    "OCT-",
                    color: .dark,
                    disabled: keyboardBaseNote <= 21
                ) {
                    shiftKeyboardOctave(-1)
                }
                .frame(height: 48)
            case .up:
                PanelButton(
                    "+1",
                    color: .dark,
                    disabled: nextWhiteKeyboardBaseNote(from: keyboardBaseNote) == keyboardBaseNote
                ) {
                    shiftKeyboardByWhiteKey(1)
                }
                .frame(height: 48)

                PanelButton(
                    "OCT+",
                    color: .dark,
                    disabled: keyboardBaseNote >= 82
                ) {
                    shiftKeyboardOctave(1)
                }
                .frame(height: 48)
            }

            Spacer(minLength: 0)
        }
        .padding(.top, 6)
        .frame(maxHeight: .infinity, alignment: .top)
    }

    private var rightPanel: some View {
        GeometryReader { proxy in
            let width = proxy.size.width
            let rowHeight: CGFloat = 28
            let buttonHeight: CGFloat = rowHeight - 4
            let buttonInset: CGFloat = 2
            let topControlHeight: CGFloat = buttonHeight
            let rowGap: CGFloat = 2
            let lcdHeight = min(CGFloat(64), max(52, proxy.size.height - rowHeight * 3 - rowGap * 4 - 30))
            let bankY: CGFloat = 0
            let lcdY = bankY + rowHeight + rowGap
            let voiceY = lcdY + lcdHeight + rowGap
            let actionY = voiceY + rowHeight + rowGap
            let perfY = actionY + rowHeight + rowGap

            ZStack(alignment: .topLeading) {
                Button {
                    showingBankPicker = true
                } label: {
                    DropdownBox(text: "\(synth.bankIndex + 1): \(synth.bankName.lowercased())")
                }
                .buttonStyle(.plain)
                .frame(width: cellWidth(width, 0, 3) - buttonInset * 2, height: topControlHeight)
                .position(x: cellMidX(width, 0, 3), y: bankY + rowHeight / 2)

                PanelButton("LOAD", color: .blue) {
                    importTarget = .voiceBank
                    importingFile = true
                }
                    .frame(width: cellWidth(width, 3, 1) - buttonInset * 2, height: buttonHeight)
                    .position(x: cellMidX(width, 3, 1), y: bankY + rowHeight / 2)

                PanelButton("SAVE", color: .blue) { exportCurrentBank() }
                    .frame(width: cellWidth(width, 4, 1) - buttonInset * 2, height: buttonHeight)
                    .position(x: cellMidX(width, 4, 1), y: bankY + rowHeight / 2)

                PanelButton("INIT", color: .red) { initializeCurrentBankFromFactory() }
                    .frame(width: cellWidth(width, 5, 1) - buttonInset * 2, height: buttonHeight)
                    .position(x: cellMidX(width, 5, 1), y: bankY + rowHeight / 2)

                LcdDisplay(line1: lcdLine1, line2: lcdLine2, scopeSamples: synth.scopeSamples)
                    .frame(width: width - buttonInset * 2, height: lcdHeight)
                    .position(x: width / 2, y: lcdY + lcdHeight / 2)

                voiceSelectorCell
                    .frame(width: cellWidth(width, 0, 3) - buttonInset * 2, height: topControlHeight)
                    .position(x: cellMidX(width, 0, 3), y: voiceY + rowHeight / 2)

                voiceNavigation(
                    previous: { changeVoice { synth.previousVoice() } },
                    next: { changeVoice { synth.nextVoice() } }
                )
                .frame(width: navigationWidth(width), height: buttonHeight)
                .position(x: cellMidX(width, 3, 1), y: voiceY + rowHeight / 2)

                PanelButton("MONO", color: synth.isMono ? .green : .dark) { synth.setMono(!synth.isMono) }
                    .frame(width: cellWidth(width, 4, 1) - buttonInset * 2, height: buttonHeight)
                    .position(x: cellMidX(width, 4, 1), y: voiceY + rowHeight / 2)

                PanelButton(synth.portamentoModeA.title, color: synth.portamentoModeA.isEnabled ? .green : .dark) {
                    synth.cyclePortamentoModeA()
                }
                    .frame(width: cellWidth(width, 5, 1) - buttonInset * 2, height: buttonHeight)
                    .position(x: cellMidX(width, 5, 1), y: voiceY + rowHeight / 2)

                if synth.performanceMode == .single {
                    actionButtons(width: width, buttonInset: buttonInset, buttonHeight: buttonHeight)
                        .position(x: width / 2, y: actionY + rowHeight / 2)
                } else {
                    voiceBSelectorCell
                        .frame(width: cellWidth(width, 0, 3) - buttonInset * 2, height: topControlHeight)
                        .position(x: cellMidX(width, 0, 3), y: actionY + rowHeight / 2)

                    voiceNavigation(
                        previous: { changeVoice { synth.previousVoiceB() } },
                        next: { changeVoice { synth.nextVoiceB() } }
                    )
                    .frame(width: navigationWidth(width), height: buttonHeight)
                    .position(x: cellMidX(width, 3, 1), y: actionY + rowHeight / 2)

                    PanelButton("MONO", color: synth.isMonoB ? .green : .dark) { synth.setMonoB(!synth.isMonoB) }
                        .frame(width: cellWidth(width, 4, 1) - buttonInset * 2, height: buttonHeight)
                        .position(x: cellMidX(width, 4, 1), y: actionY + rowHeight / 2)

                    PanelButton(synth.portamentoModeB.title, color: synth.portamentoModeB.isEnabled ? .green : .dark) {
                        synth.cyclePortamentoModeB()
                    }
                    .frame(width: cellWidth(width, 5, 1) - buttonInset * 2, height: buttonHeight)
                    .position(x: cellMidX(width, 5, 1), y: actionY + rowHeight / 2)
                }

                HStack(spacing: 10) {
                    ModeDropdownBox(text: synth.performanceMode.title) {
                        showingModePicker = true
                    }
                        .frame(width: min(120, width / 3), height: topControlHeight)

                    if synth.performanceMode == .dual {
                        PerformanceParameterControl(
                            title: "Detune",
                            value: synth.dualDetune,
                            range: -16...16,
                            valueText: "\(synth.dualDetune)"
                        ) { synth.setDualDetune($0) }
                    } else if synth.performanceMode == .split {
                        PerformanceParameterControl(
                            title: "Split",
                            value: synth.splitPoint,
                            range: 0...127,
                            valueText: noteName(synth.splitPoint)
                        ) { synth.setSplitPoint($0) }
                    }

                    Spacer()
                }
                .frame(
                    width: width - buttonInset * 2,
                    height: topControlHeight,
                    alignment: .leading
                )
                .position(x: width / 2, y: perfY + rowHeight / 2)
            }
        }
    }

    private var playEditSwitch: some View {
        HStack(spacing: 2) {
            modeSwitchButton("PLAY", active: true) {}
            modeSwitchButton("EDIT", active: false) {
                synth.screen = .edit
            }
        }
        .padding(2)
        .background(
            RoundedRectangle(cornerRadius: 4)
                .fill(Color(hex: 0x070806))
        )
        .overlay(RoundedRectangle(cornerRadius: 4).stroke(Color(hex: 0x5d5445).opacity(0.65), lineWidth: 1))
    }

    private func modeSwitchButton(_ title: String, active: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 12, weight: .bold))
                .foregroundStyle(active ? Color.white : MacSkin.textMuted)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(
                    RoundedRectangle(cornerRadius: 3)
                        .fill(LinearGradient(
                            colors: active
                                ? [Color(hex: 0x1fa08b), Color(hex: 0x0d5d4f)]
                                : [Color(hex: 0x34352f), Color(hex: 0x22231e)],
                            startPoint: .top,
                            endPoint: .bottom
                        ))
                )
                .overlay(RoundedRectangle(cornerRadius: 3).stroke(active ? Color(hex: 0x25d9c4).opacity(0.65) : Color.black.opacity(0.45), lineWidth: 1))
        }
        .buttonStyle(.plain)
    }

    private var voiceSelectorCell: some View {
        HStack(spacing: 4) {
            VoiceBadge("A")
                .frame(width: 22, height: 22)
            VoiceDropdownBox(
                text: currentVoiceText,
                action: { activeVoicePicker = .voiceA }
            )
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }

    private var voiceBSelectorCell: some View {
        HStack(spacing: 4) {
            VoiceBadge("B")
                .frame(width: 22, height: 22)
            VoiceDropdownBox(
                text: currentVoiceBText,
                action: { activeVoicePicker = .voiceB }
            )
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }

    private func cellWidth(_ totalWidth: CGFloat, _ column: Int, _ span: Int) -> CGFloat {
        let left = totalWidth * CGFloat(column) / 6
        let right = totalWidth * CGFloat(column + span) / 6
        return right - left
    }

    private func cellMidX(_ totalWidth: CGFloat, _ column: Int, _ span: Int) -> CGFloat {
        let left = totalWidth * CGFloat(column) / 6
        let right = totalWidth * CGFloat(column + span) / 6
        return (left + right) / 2
    }

    private func navigationWidth(_ totalWidth: CGFloat) -> CGFloat {
        max(44, cellWidth(totalWidth, 3, 1) - 4)
    }

    private func voiceNavigation(previous: @escaping () -> Void, next: @escaping () -> Void) -> some View {
        HStack(spacing: 4) {
            NavigationButton("<", action: previous)
            NavigationButton(">", action: next)
        }
    }

    private func actionButtons(width: CGFloat, buttonInset: CGFloat, buttonHeight: CGFloat) -> some View {
        ZStack(alignment: .topLeading) {
            PanelButton("LOAD", color: .blue) {
                importTarget = .singleVoice
                importingFile = true
            }
                .frame(width: cellWidth(width, 0, 1) - buttonInset * 2, height: buttonHeight)
                .position(x: cellMidX(width, 0, 1), y: buttonHeight / 2)
            PanelButton("SAVE", color: .blue) { exportSingleVoice() }
                .frame(width: cellWidth(width, 1, 1) - buttonInset * 2, height: buttonHeight)
                .position(x: cellMidX(width, 1, 1), y: buttonHeight / 2)
            PanelButton("COPY", color: .blue) { synth.copyCurrentVoice() }
                .frame(width: cellWidth(width, 2, 1) - buttonInset * 2, height: buttonHeight)
                .position(x: cellMidX(width, 2, 1), y: buttonHeight / 2)
            PanelButton("PASTE", color: synth.canPasteVoice ? .blue : .dark) {
                synth.pasteCopiedVoice()
            }
            .frame(width: cellWidth(width, 3, 1) - buttonInset * 2, height: buttonHeight)
            .position(x: cellMidX(width, 3, 1), y: buttonHeight / 2)
            PanelButton("INIT", color: .blue) { synth.initializeCurrentVoice() }
                .frame(width: cellWidth(width, 4, 1) - buttonInset * 2, height: buttonHeight)
                .position(x: cellMidX(width, 4, 1), y: buttonHeight / 2)
            PanelButton("STORE", color: .red) { synth.storeCurrentVoice() }
                .frame(width: cellWidth(width, 5, 1) - buttonInset * 2, height: buttonHeight)
                .position(x: cellMidX(width, 5, 1), y: buttonHeight / 2)
        }
        .frame(width: width, height: buttonHeight)
    }

    private var wheelPanel: some View {
        HStack(spacing: 8) {
            WheelFader(title: "PITCH", value: Binding(
                get: { synth.pitchWheel },
                set: { synth.setPitchWheel($0) }
            ), range: -1...1, resetOnRelease: true)
            .frame(width: 46)
            .frame(maxHeight: .infinity)

            WheelFader(title: "MODULATION", value: Binding(
                get: { synth.modWheel },
                set: { synth.setModWheel($0) }
            ), range: 0...1)
            .frame(width: 66)
            .frame(maxHeight: .infinity)

            VolumeSlider(value: Binding(
                get: { synth.masterVolume },
                set: { synth.setMasterVolume($0) }
            ))
            .frame(width: 46)
            .frame(maxHeight: .infinity)
        }
        .padding(.horizontal, 7)
        .padding(.vertical, 6)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(MetalPanel())
    }

    private var knobPanel: some View {
        GeometryReader { proxy in
            let columnWidth = proxy.size.width / 2
            let topY = max(35, proxy.size.height * 0.30)
            let bottomY = min(proxy.size.height - 38, proxy.size.height * 0.74)

            ZStack {
                ParameterKnob(
                    title: "AB BAL",
                    value: Int(synth.balance.rounded()),
                    range: -100...100,
                    defaultValue: 0,
                    action: { synth.setBalance(Double($0)) }
                )
                .position(x: columnWidth * 0.5, y: topY)

                ParameterKnob(
                    title: "RANGE",
                    value: synth.pitchBendRange,
                    range: 0...12,
                    defaultValue: 2,
                    action: { synth.setPitchBendRange($0) }
                )
                .position(x: columnWidth * 1.5, y: topY)

                ParameterKnob(
                    title: "TRANSPOSE",
                    value: Int(synth.transpose.rounded()),
                    range: -24...24,
                    defaultValue: 0,
                    action: { synth.setTranspose(Double($0)) }
                )
                .position(x: columnWidth * 0.5, y: bottomY)

                ParameterKnob(
                    title: "PORTA",
                    value: synth.portamento,
                    range: 0...99,
                    defaultValue: 0,
                    action: { synth.setPortamento($0) }
                )
                .position(x: columnWidth * 1.5, y: bottomY)
            }
        }
    }

    private func keyboardPanel(width: CGFloat, height: CGFloat) -> some View {
        MobileKeyboardView(
            baseNote: $keyboardBaseNote,
            visibleWhiteKeyCount: keyboardVisibleWhiteKeyCount,
            scrollWhiteIndex: keyboardScrollWhiteIndex
        )
            .environmentObject(synth)
            .frame(width: width, height: height)
            .clipped()
            .layoutPriority(1)
    }

    private func octaveCornerControls(size: CGSize, height: CGFloat) -> some View {
        let leftWidth = max(44, min(64, screenSafeAreaInsets.left - 8))
        let rightWidth = max(44, min(64, screenSafeAreaInsets.right - 8))
        let buttonHeight = max(88, height * 0.5)
        let cornerClearance: CGFloat = 44

        return HStack(alignment: .top, spacing: 0) {
            VStack(spacing: 4) {
                CornerOctaveButton(
                    title: "-1",
                    disabled: previousWhiteKeyboardBaseNote(from: keyboardBaseNote) == keyboardBaseNote,
                    action: { shiftKeyboardByWhiteKey(-1) }
                )

                CornerOctaveButton(
                    title: "OCT-",
                    disabled: keyboardBaseNote <= 21,
                    action: { shiftKeyboardOctave(-1) }
                )
            }
            .frame(width: leftWidth, height: buttonHeight)

            Spacer(minLength: 0)

            VStack(spacing: 4) {
                CornerOctaveButton(
                    title: "+1",
                    disabled: nextWhiteKeyboardBaseNote(from: keyboardBaseNote) == keyboardBaseNote,
                    action: { shiftKeyboardByWhiteKey(1) }
                )

                CornerOctaveButton(
                    title: "OCT+",
                    disabled: keyboardBaseNote >= 82,
                    action: { shiftKeyboardOctave(1) }
                )
            }
            .frame(width: rightWidth, height: buttonHeight)
        }
        .frame(width: size.width, height: size.height, alignment: .top)
        .offset(y: cornerClearance)
        .ignoresSafeArea()
        .allowsHitTesting(true)
    }

    private func shiftKeyboardByWhiteKey(_ delta: Int) {
        let nextBaseNote = delta > 0
            ? nextWhiteKeyboardBaseNote(from: keyboardBaseNote)
            : previousWhiteKeyboardBaseNote(from: keyboardBaseNote)
        setKeyboardBaseNote(nextBaseNote, duration: 0.28)
    }

    private func shiftKeyboardOctave(_ delta: Int) {
        let nextBaseNote: Int
        if delta > 0 {
            if keyboardBaseNote >= 77 {
                nextBaseNote = 82
            } else if keyboardBaseNote == 21 {
                nextBaseNote = 29
            } else {
                nextBaseNote = min(77, keyboardBaseNote + 12)
            }
        } else if delta < 0 {
            if keyboardBaseNote == 82 {
                nextBaseNote = 77
            } else if keyboardBaseNote <= 29 {
                nextBaseNote = 21
            } else {
                nextBaseNote = max(29, keyboardBaseNote - 12)
            }
        } else {
            nextBaseNote = keyboardBaseNote
        }
        setKeyboardBaseNote(nextBaseNote, duration: 0.16)
    }

    private func setKeyboardBaseNote(_ note: Int, duration: Double) {
        guard note != keyboardBaseNote else { return }
        withAnimation(.easeInOut(duration: duration)) {
            keyboardBaseNote = note
            keyboardScrollWhiteIndex = CGFloat(clampedWhiteKeyIndexFromKeyboardStart(note))
        }
    }

    private func changeVoice(_ action: () -> Void) {
        action()
        resetKeyboardToHome()
    }

    private func resetKeyboardToHome() {
        let homeNote = 48
        let homeWhiteIndex = CGFloat(clampedWhiteKeyIndexFromKeyboardStart(homeNote))
        guard keyboardBaseNote != homeNote || keyboardScrollWhiteIndex != homeWhiteIndex else { return }
        withAnimation(.easeInOut(duration: 0.18)) {
            keyboardBaseNote = homeNote
            keyboardScrollWhiteIndex = homeWhiteIndex
        }
    }

    private func clampedWhiteKeyIndexFromKeyboardStart(_ note: Int) -> Int {
        let index = (21..<note).filter { isWhiteKey($0) }.count
        let whiteKeyTotal = (21...108).filter { isWhiteKey($0) }.count
        return min(max(0, index), max(0, whiteKeyTotal - keyboardVisibleWhiteKeyCount))
    }

    private func nextWhiteKeyboardBaseNote(from note: Int) -> Int {
        guard note < 82 else { return note }
        var candidate = note + 1
        while candidate <= 82 {
            if isWhiteKey(candidate) {
                return candidate
            }
            candidate += 1
        }
        return note
    }

    private func previousWhiteKeyboardBaseNote(from note: Int) -> Int {
        guard note > 21 else { return note }
        var candidate = note - 1
        while candidate >= 21 {
            if isWhiteKey(candidate) {
                return candidate
            }
            candidate -= 1
        }
        return note
    }

    private func isWhiteKey(_ note: Int) -> Bool {
        ![1, 3, 6, 8, 10].contains(note % 12)
    }

    private var currentVoiceText: String {
        "\(synth.voiceIndex + 1) \(synth.voiceName)"
    }

    private var currentLcdVoiceText: String {
        lcdVoiceText(index: synth.voiceIndex, name: synth.voiceName)
    }

    private var currentVoiceBText: String {
        "\(synth.voiceBIndex + 1) \(synth.voiceBName)"
    }

    private var currentLcdVoiceBText: String {
        lcdVoiceText(index: synth.voiceBIndex, name: synth.voiceBName)
    }

    private var lcdLine1: String {
        switch synth.performanceMode {
        case .single:
            return "PLAY SINGLE"
        case .dual:
            return "DU:" + currentLcdVoiceText
        case .split:
            return "SP:" + currentLcdVoiceText
        }
    }

    private var lcdLine2: String {
        switch synth.performanceMode {
        case .single:
            return currentLcdVoiceText
        case .dual:
            return String(format: "%2d:%@", synth.dualDetune, currentLcdVoiceBText)
        case .split:
            return String(format: "%2d:%@", synth.splitPoint, currentLcdVoiceBText)
        }
    }

    private func lcdVoiceText(index: Int, name: String) -> String {
        String(format: "%2d %@", index + 1, name)
    }

    private var importContentTypes: [UTType] {
        switch importTarget {
        case .voiceBank:
            return [.opalineSysexBank, .data]
        case .singleVoice:
            return [.opalineVoice, .xml, .data]
        case nil:
            return [.data]
        }
    }

    private var opalineDocumentsDirectory: URL? {
        guard let documentsURL = FileManager.default.urls(
            for: .documentDirectory,
            in: .userDomainMask
        ).first else {
            return nil
        }

        let directoryURL = documentsURL.appendingPathComponent("OpalineFM", isDirectory: true)
        do {
            try FileManager.default.createDirectory(
                at: directoryURL,
                withIntermediateDirectories: true
            )
            installFactoryBankIfNeeded(in: directoryURL)
            return directoryURL
        } catch {
            return nil
        }
    }

    private func installFactoryBankIfNeeded(in directoryURL: URL) {
        let factoryURL = directoryURL.appendingPathComponent("factory.syx")
        let fileManager = FileManager.default

        if !fileManager.fileExists(atPath: factoryURL.path),
           let bundledFactoryURL = Bundle.main.url(forResource: "factory", withExtension: "syx") {
            try? fileManager.copyItem(at: bundledFactoryURL, to: factoryURL)
        }

        guard fileManager.fileExists(atPath: factoryURL.path) else { return }
        try? fileManager.setAttributes(
            [.posixPermissions: NSNumber(value: Int16(0o444))],
            ofItemAtPath: factoryURL.path
        )
    }

    private func handleImport(_ result: Result<URL, Error>) {
        let target = importTarget
        importTarget = nil
        importingFile = false

        switch target {
        case .voiceBank:
            handleVoiceBankImport(result)
        case .singleVoice:
            handleSingleVoiceImport(result)
        case nil:
            break
        }
    }

    private func handleVoiceBankImport(_ result: Result<URL, Error>) {
        guard case let .success(url) = result else { return }
        let accessing = url.startAccessingSecurityScopedResource()
        defer {
            if accessing {
                url.stopAccessingSecurityScopedResource()
            }
        }

        do {
            let data = try Data(contentsOf: url)
            let loaded = synth.loadVoiceBank(data: data, fileName: url.deletingPathExtension().lastPathComponent)
            if loaded {
                resetKeyboardToHome()
            }
            synth.audioStatus = loaded ? "Bank loaded" : "Bank load failed"
        } catch {
            synth.audioStatus = "Bank load failed"
        }
    }

    private func initializeCurrentBankFromFactory() {
        guard let factoryURL = factoryBankURL() else {
            synth.audioStatus = "Factory bank not found"
            return
        }

        do {
            let data = try Data(contentsOf: factoryURL)
            let loaded = synth.loadVoiceBank(data: data, fileName: "Factory")
            if loaded {
                resetKeyboardToHome()
            }
            synth.audioStatus = loaded ? "Bank initialized" : "Bank init failed"
        } catch {
            synth.audioStatus = "Bank init failed"
        }
    }

    private func factoryBankURL() -> URL? {
        if let directoryURL = opalineDocumentsDirectory {
            let installedURL = directoryURL.appendingPathComponent("factory.syx")
            if FileManager.default.fileExists(atPath: installedURL.path) {
                return installedURL
            }
        }
        return Bundle.main.url(forResource: "factory", withExtension: "syx")
    }

    private func handleSingleVoiceImport(_ result: Result<URL, Error>) {
        guard case let .success(url) = result else { return }
        let accessing = url.startAccessingSecurityScopedResource()
        defer {
            if accessing {
                url.stopAccessingSecurityScopedResource()
            }
        }

        do {
            let data = try Data(contentsOf: url)
            let loaded = synth.loadSingleVoice(data: data, fileName: url.deletingPathExtension().lastPathComponent)
            if loaded {
                resetKeyboardToHome()
            }
            synth.audioStatus = loaded ? "Single voice loaded" : "Single voice load failed"
        } catch {
            synth.audioStatus = "Single voice load failed"
        }
    }

    private func exportSingleVoice() {
        let name = sanitizedFilenamePart(synth.voiceName)
        let filename = name.isEmpty ? "OpalineFM_Voice.opalinevoice" : "\(name).opalinevoice"
        prepareExport(data: synth.currentSingleVoiceXMLData(), filename: filename)
    }

    private func exportCurrentBank() {
        let bankName = sanitizedFilenamePart(synth.bankName)
        let filename = bankName.isEmpty || bankName.lowercased() == "factory"
            ? "OpalineFM_Bank_\(synth.bankIndex + 1).syx"
            : "\(bankName).syx"
        prepareExport(data: synth.currentVoiceBankSysexData(), filename: filename)
    }

    private func exportVoiceLibrary() {
        prepareExport(
            data: synth.voiceLibraryXMLData(),
            filename: "OpalineFM_Voice_Library.opalinelibrary.xml"
        )
    }

    private func prepareExport(data: Data, filename: String) {
        guard opalineDocumentsDirectory != nil else {
            synth.audioStatus = "OpalineFM folder creation failed"
            return
        }

        let safeFilename = filename.lowercased() == "factory.syx" ? "Factory Copy.syx" : filename
        let temporaryDirectory = FileManager.default.temporaryDirectory
            .appendingPathComponent("OpalineFMExports", isDirectory: true)
        let fileURL = temporaryDirectory.appendingPathComponent(safeFilename)

        do {
            try FileManager.default.createDirectory(
                at: temporaryDirectory,
                withIntermediateDirectories: true
            )
            try data.write(to: fileURL, options: .atomic)
            exportFileURL = fileURL
            exportingData = true
        } catch {
            synth.audioStatus = "File save failed"
        }
    }

    private func finishExport(savedURL: URL?) {
        if let exportFileURL {
            try? FileManager.default.removeItem(at: exportFileURL)
        }
        exportFileURL = nil
        exportingData = false
        if savedURL != nil {
            synth.audioStatus = "File saved"
        }
    }

    private func sanitizedFilenamePart(_ name: String) -> String {
        let invalid = CharacterSet(charactersIn: "/\\?%*|\"<>:")
        return name
            .components(separatedBy: invalid)
            .joined(separator: "_")
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private func noteName(_ midiNote: Int) -> String {
        let names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        let safeNote = max(0, min(127, midiNote))
        let octave = safeNote / 12 - 1
        return "\(names[safeNote % 12])\(octave)"
    }

    private func voicePickerOverlay(target: VoicePickerTarget, size: CGSize) -> some View {
        ZStack {
            Color.black.opacity(0.34)
                .ignoresSafeArea()
                .contentShape(Rectangle())
                .onTapGesture {
                    activeVoicePicker = nil
                }

            VoicePickerPanel(
                title: target == .voiceA ? "VOICE A" : "VOICE B",
                names: synth.voiceNames,
                selectedIndex: target == .voiceA ? synth.voiceIndex : synth.voiceBIndex,
                onSelect: { index in
                    changeVoice {
                        if target == .voiceA {
                            synth.selectVoice(bank: synth.bankIndex, voice: index)
                        } else {
                            synth.selectVoiceB(index)
                        }
                    }
                    activeVoicePicker = nil
                },
                onClose: {
                    activeVoicePicker = nil
                }
            )
            .frame(width: min(360, max(280, size.width * 0.48)), height: min(360, max(260, size.height - 34)))
        }
        .zIndex(20)
    }

    private func bankPickerOverlay(size: CGSize) -> some View {
        ZStack {
            Color.black.opacity(0.34)
                .ignoresSafeArea()
                .contentShape(Rectangle())
                .onTapGesture {
                    showingBankPicker = false
                }

            OptionPickerPanel(
                title: "VOICE BANK",
                options: bankOptions,
                selectedIndex: synth.bankIndex,
                onSelect: { index in
                    changeVoice {
                        synth.selectBank(index)
                    }
                    showingBankPicker = false
                },
                onClose: {
                    showingBankPicker = false
                }
            )
            .frame(width: min(360, max(280, size.width * 0.45)), height: 330)
        }
        .zIndex(20)
    }

    private func modePickerOverlay(size: CGSize) -> some View {
        ZStack {
            Color.black.opacity(0.34)
                .ignoresSafeArea()
                .contentShape(Rectangle())
                .onTapGesture {
                    showingModePicker = false
                }

            OptionPickerPanel(
                title: "PERFORMANCE",
                options: MobileSynthModel.PerformanceMode.allCases.map(\.title),
                selectedIndex: synth.performanceMode.rawValue,
                onSelect: { index in
                    guard let mode = MobileSynthModel.PerformanceMode(rawValue: index) else { return }
                    synth.setPerformanceMode(mode)
                    showingModePicker = false
                },
                onClose: {
                    showingModePicker = false
                }
            )
            .frame(width: min(300, max(240, size.width * 0.38)), height: 176)
        }
        .zIndex(21)
    }

    private var bankOptions: [String] {
        (0..<8).map { index in
            let name = index < synth.bankNames.count ? synth.bankNames[index] : "Bank \(index + 1)"
            return "\(index + 1): \(name)"
        }
    }
}

private enum VoicePickerTarget {
    case voiceA
    case voiceB
}

private enum KeyboardShiftDirection {
    case down
    case up
}

private enum ImportTarget {
    case voiceBank
    case singleVoice
}

private struct OpalineDocumentPicker: UIViewControllerRepresentable {
    enum Mode {
        case importing([UTType])
        case exporting(URL)
    }

    let mode: Mode
    let initialDirectoryURL: URL?
    let completion: (URL?) -> Void

    func makeCoordinator() -> Coordinator {
        Coordinator(completion: completion)
    }

    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        let picker: UIDocumentPickerViewController
        switch mode {
        case let .importing(contentTypes):
            picker = UIDocumentPickerViewController(
                forOpeningContentTypes: contentTypes,
                asCopy: true
            )
        case let .exporting(fileURL):
            picker = UIDocumentPickerViewController(
                forExporting: [fileURL],
                asCopy: true
            )
        }
        picker.delegate = context.coordinator
        picker.allowsMultipleSelection = false
        picker.directoryURL = initialDirectoryURL
        return picker
    }

    func updateUIViewController(
        _ uiViewController: UIDocumentPickerViewController,
        context: Context
    ) {}

    final class Coordinator: NSObject, UIDocumentPickerDelegate {
        let completion: (URL?) -> Void

        init(completion: @escaping (URL?) -> Void) {
            self.completion = completion
        }

        func documentPicker(
            _ controller: UIDocumentPickerViewController,
            didPickDocumentsAt urls: [URL]
        ) {
            completion(urls.first)
        }

        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
            completion(nil)
        }
    }
}

private extension UTType {
    static let opalineSysexBank = UTType(filenameExtension: "syx") ?? .data
    static let opalineVoice = UTType(filenameExtension: "opalinevoice") ?? .xml
    static let opalineVoiceLibrary = UTType(filenameExtension: "xml") ?? .xml
}

private enum MacSkin {
    static let appBackground = Color(hex: 0x020405)
    static let panelTop = Color(hex: 0x29261f)
    static let panelBottom = Color(hex: 0x14130f)
    static let panelBorder = Color(hex: 0x554f40).opacity(0.40)
    static let textPrimary = Color(hex: 0xedf4f3)
    static let textMuted = Color(hex: 0xb7c2bd)
    static let valueText = Color(hex: 0xffffd52b)
    static let teal = Color(hex: 0x25d9c4)
    static let lcdOn = Color(hex: 0x94e7ff)
    static let lcdOff = Color(hex: 0x164372).opacity(0.42)
}

private extension Color {
    init(hex: UInt32) {
        self.init(
            red: Double((hex >> 16) & 0xff) / 255.0,
            green: Double((hex >> 8) & 0xff) / 255.0,
            blue: Double(hex & 0xff) / 255.0
        )
    }
}

private struct CornerOctaveButton: View {
    let title: String
    let disabled: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 13, weight: .heavy, design: .monospaced))
                .foregroundStyle(MacSkin.textPrimary)
                .lineLimit(1)
                .minimumScaleFactor(0.7)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(
                    RoundedRectangle(cornerRadius: 3)
                        .fill(LinearGradient(
                            colors: [Color(hex: 0x34352f), Color(hex: 0x191a15)],
                            startPoint: .top,
                            endPoint: .bottom
                        ))
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 3)
                        .stroke(Color.black.opacity(0.75), lineWidth: 1.2)
                )
        }
        .buttonStyle(.plain)
        .disabled(disabled)
        .opacity(disabled ? 0.45 : 1)
    }
}

private struct MetalPanel: View {
    var body: some View {
        RoundedRectangle(cornerRadius: 3)
            .fill(LinearGradient(colors: [MacSkin.panelTop, MacSkin.panelBottom], startPoint: .top, endPoint: .bottom))
            .overlay(RoundedRectangle(cornerRadius: 3).stroke(MacSkin.panelBorder, lineWidth: 1))
            .shadow(color: .black.opacity(0.45), radius: 2, x: 0, y: 2)
    }
}

private struct MainFader: View {
    let title: String
    let top: String
    let bottom: String
    @Binding var value: Double
    let range: ClosedRange<Double>

    var body: some View {
        VStack(spacing: 5) {
            GeometryReader { proxy in
                ZStack {
                    RoundedRectangle(cornerRadius: 2)
                        .fill(Color(hex: 0x050504))
                        .overlay(RoundedRectangle(cornerRadius: 2).stroke(Color.black.opacity(0.85), lineWidth: 1))

                    RoundedRectangle(cornerRadius: 1)
                        .fill(LinearGradient(colors: [Color(hex: 0x12110e), Color(hex: 0x1b1914)], startPoint: .top, endPoint: .bottom))
                        .padding(3)

                    sliderSlot(proxy: proxy)
                    tickMarks(proxy: proxy)
                    topBottomLabels(proxy: proxy)
                    thumb(proxy: proxy)
                }
                .contentShape(Rectangle())
                .gesture(drag(proxy: proxy))
            }

            Text(title)
                .font(.system(size: 10, weight: .bold))
                .foregroundStyle(MacSkin.textPrimary)
                .lineLimit(1)
                .minimumScaleFactor(0.7)
        }
    }

    private func sliderSlot(proxy: GeometryProxy) -> some View {
        let h = max(10, proxy.size.height - 28)
        return RoundedRectangle(cornerRadius: 2)
            .fill(LinearGradient(colors: [Color(hex: 0x020304), Color(hex: 0x141519)], startPoint: .top, endPoint: .bottom))
            .frame(width: 5, height: h)
            .position(x: proxy.size.width * 0.34, y: proxy.size.height / 2)
    }

    private func tickMarks(proxy: GeometryProxy) -> some View {
        let slotH = max(10, proxy.size.height - 28)
        let startY: CGFloat = 14
        return ZStack(alignment: .topLeading) {
            ForEach(0..<11, id: \.self) { index in
                let center = index == 5 && range.lowerBound < 0
                Rectangle()
                    .fill(center ? Color(hex: 0xd6d5df).opacity(0.82) : Color(hex: 0xaaa8b8).opacity(0.55))
                    .frame(width: center ? 16 : (index % 5 == 0 ? 13 : 9), height: center ? 2 : 1.4)
                    .offset(x: proxy.size.width * 0.62, y: startY + slotH * CGFloat(index) / 10)
            }
        }
    }

    private func topBottomLabels(proxy: GeometryProxy) -> some View {
        VStack {
            Text(top)
            Spacer()
            Text(bottom)
        }
        .font(.system(size: 8.5, weight: .bold))
        .foregroundStyle(MacSkin.textPrimary)
        .frame(width: proxy.size.width * 0.35, height: proxy.size.height - 4)
        .position(x: proxy.size.width * 0.78, y: proxy.size.height / 2)
    }

    private func thumb(proxy: GeometryProxy) -> some View {
        let ratio = normalizedValue
        let y = (proxy.size.height - 24) - (proxy.size.height - 48) * ratio
        return RoundedRectangle(cornerRadius: 1.2)
            .fill(LinearGradient(colors: [Color(hex: 0xbfc8ca), Color(hex: 0x657074)], startPoint: .top, endPoint: .bottom))
            .frame(width: proxy.size.width * 0.55, height: 9)
            .overlay(Rectangle().fill(Color.white.opacity(0.35)).frame(height: 1), alignment: .top)
            .overlay(RoundedRectangle(cornerRadius: 1.2).stroke(Color(hex: 0x202528), lineWidth: 1))
            .shadow(color: .black.opacity(0.6), radius: 1, x: 0, y: 2)
            .position(x: proxy.size.width * 0.30, y: y)
    }

    private func drag(proxy: GeometryProxy) -> some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { gesture in
                let y = min(max(gesture.location.y, 18), proxy.size.height - 18)
                let normalized = 1.0 - (y - 18) / max(1, proxy.size.height - 36)
                value = range.lowerBound + Double(normalized) * (range.upperBound - range.lowerBound)
            }
    }

    private var normalizedValue: CGFloat {
        CGFloat((value - range.lowerBound) / max(0.0001, range.upperBound - range.lowerBound))
    }
}

private enum ButtonTone {
    case blue
    case green
    case red
    case dark
}

private struct PanelButton: View {
    let title: String
    let color: ButtonTone
    let disabled: Bool
    let action: () -> Void

    init(_ title: String, color: ButtonTone, disabled: Bool = false, action: @escaping () -> Void) {
        self.title = title
        self.color = color
        self.disabled = disabled
        self.action = action
    }

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 12, weight: .bold))
                .foregroundStyle(color == .dark ? MacSkin.textMuted : Color(hex: 0xdcfff1))
                .lineLimit(1)
                .minimumScaleFactor(0.6)
                .frame(minWidth: 36, maxWidth: .infinity, maxHeight: .infinity)
                .padding(.horizontal, 6)
                .background(buttonFill)
                .overlay(RoundedRectangle(cornerRadius: 3).stroke(Color.black.opacity(0.55), lineWidth: 1))
        }
        .buttonStyle(.plain)
        .disabled(disabled)
        .opacity(disabled ? 0.45 : 1)
    }

    private var buttonFill: some View {
        RoundedRectangle(cornerRadius: 3)
            .fill(LinearGradient(colors: gradientColors, startPoint: .top, endPoint: .bottom))
    }

    private var gradientColors: [Color] {
        switch color {
        case .blue:
            return [Color(hex: 0x327fa8), Color(hex: 0x14425b)]
        case .green:
            return [Color(hex: 0x23876a), Color(hex: 0x0d4939)]
        case .red:
            return [Color(hex: 0x87383a), Color(hex: 0x541d20)]
        case .dark:
            return [Color(hex: 0x454640), Color(hex: 0x23241f)]
        }
    }
}

private struct NavigationButton: View {
    let title: String
    let action: () -> Void

    init(_ title: String, action: @escaping () -> Void) {
        self.title = title
        self.action = action
    }

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 12, weight: .bold))
                .foregroundStyle(Color(hex: 0xdcfff1))
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(
                    RoundedRectangle(cornerRadius: 3)
                        .fill(LinearGradient(
                            colors: [Color(hex: 0x23876a), Color(hex: 0x0d4939)],
                            startPoint: .top,
                            endPoint: .bottom
                        ))
                )
                .overlay(RoundedRectangle(cornerRadius: 3).stroke(Color.black.opacity(0.55), lineWidth: 1))
        }
        .buttonStyle(.plain)
    }
}

private struct DropdownBox: View {
    let text: String

    var body: some View {
        HStack {
            Text(text)
                .font(.system(size: 14, weight: .bold))
                .foregroundStyle(MacSkin.textPrimary)
                .lineLimit(1)
                .minimumScaleFactor(0.65)
            Spacer(minLength: 6)
            Image(systemName: "chevron.down")
                .font(.system(size: 13, weight: .bold))
                .foregroundStyle(MacSkin.textMuted)
        }
        .padding(.horizontal, 8)
        .frame(maxHeight: .infinity)
        .background(RoundedRectangle(cornerRadius: 3).fill(Color(hex: 0x11100d)))
        .overlay(RoundedRectangle(cornerRadius: 3).stroke(Color(hex: 0x403728), lineWidth: 1))
    }
}

private struct ModeDropdownBox: View {
    let text: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            DropdownBox(text: text)
        }
        .buttonStyle(.plain)
    }
}

private struct VoiceDropdownBox: View {
    let text: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            DropdownBox(text: text)
        }
        .buttonStyle(.plain)
    }
}

private struct VoicePickerPanel: View {
    let title: String
    let names: [String]
    let selectedIndex: Int
    let onSelect: (Int) -> Void
    let onClose: () -> Void

    var body: some View {
        VStack(spacing: 6) {
            HStack {
                Text(title)
                    .font(.system(size: 15, weight: .heavy, design: .monospaced))
                    .foregroundStyle(MacSkin.textPrimary)

                Spacer()

                Button("CLOSE", action: onClose)
                    .buttonStyle(.plain)
                    .font(.system(size: 11, weight: .heavy, design: .monospaced))
                    .foregroundStyle(MacSkin.textMuted)
                    .frame(width: 58, height: 24)
                    .background(RoundedRectangle(cornerRadius: 3).fill(Color(hex: 0x2b2c27)))
                    .overlay(RoundedRectangle(cornerRadius: 3).stroke(Color.black.opacity(0.65), lineWidth: 1))
            }
            .padding(.horizontal, 8)
            .padding(.top, 7)

            ScrollViewReader { proxy in
                ScrollView(.vertical, showsIndicators: true) {
                    LazyVStack(spacing: 2) {
                        ForEach(0..<32, id: \.self) { index in
                            Button {
                                onSelect(index)
                            } label: {
                                HStack(spacing: 8) {
                                    Text(String(format: "%02d", index + 1))
                                        .font(.system(size: 14, weight: .heavy, design: .monospaced))
                                        .foregroundStyle(index == selectedIndex ? Color(hex: 0x0b1c1a) : MacSkin.valueText)
                                        .frame(width: 34, alignment: .trailing)

                                    Text(voiceName(index: index))
                                        .font(.system(size: 14, weight: .bold, design: .monospaced))
                                        .foregroundStyle(index == selectedIndex ? Color(hex: 0x0b1c1a) : MacSkin.textPrimary)
                                        .lineLimit(1)

                                    Spacer(minLength: 0)

                                    if index == selectedIndex {
                                        Text("SELECTED")
                                            .font(.system(size: 9, weight: .heavy, design: .monospaced))
                                            .foregroundStyle(Color(hex: 0x0b1c1a).opacity(0.72))
                                    }
                                }
                                .padding(.horizontal, 8)
                                .frame(height: 28)
                                .background(
                                    RoundedRectangle(cornerRadius: 3)
                                        .fill(index == selectedIndex
                                              ? LinearGradient(colors: [Color(hex: 0x32e6c8), Color(hex: 0x15937f)], startPoint: .top, endPoint: .bottom)
                                              : LinearGradient(colors: [Color(hex: 0x171812), Color(hex: 0x0c0c09)], startPoint: .top, endPoint: .bottom))
                                )
                                .overlay(
                                    RoundedRectangle(cornerRadius: 3)
                                        .stroke(index == selectedIndex ? Color(hex: 0x8df8ea).opacity(0.7) : Color(hex: 0x403728), lineWidth: 1)
                                )
                            }
                            .buttonStyle(.plain)
                            .id(index)
                        }
                    }
                    .padding(.horizontal, 8)
                    .padding(.bottom, 8)
                }
                .onAppear {
                    proxy.scrollTo(selectedIndex, anchor: .center)
                }
            }
        }
        .background(
            RoundedRectangle(cornerRadius: 5)
                .fill(LinearGradient(colors: [Color(hex: 0x27261f), Color(hex: 0x10100c)], startPoint: .top, endPoint: .bottom))
        )
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(Color(hex: 0x3edcca).opacity(0.68), lineWidth: 1.5))
        .shadow(color: Color.black.opacity(0.55), radius: 12, x: 0, y: 8)
    }

    private func voiceName(index: Int) -> String {
        let name = index < names.count ? names[index] : "Voice"
        return name
    }
}

private struct OptionPickerPanel: View {
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
                    .foregroundStyle(MacSkin.textPrimary)

                Spacer()

                Button("CLOSE", action: onClose)
                    .buttonStyle(.plain)
                    .font(.system(size: 11, weight: .heavy, design: .monospaced))
                    .foregroundStyle(MacSkin.textMuted)
                    .frame(width: 58, height: 24)
                    .background(RoundedRectangle(cornerRadius: 3).fill(Color(hex: 0x2b2c27)))
                    .overlay(RoundedRectangle(cornerRadius: 3).stroke(Color.black.opacity(0.65), lineWidth: 1))
            }
            .padding(.horizontal, 8)
            .padding(.top, 7)

            VStack(spacing: 3) {
                ForEach(options.indices, id: \.self) { index in
                    Button {
                        onSelect(index)
                    } label: {
                        HStack {
                            Text(options[index])
                                .font(.system(size: 14, weight: .heavy, design: .monospaced))
                                .foregroundStyle(index == selectedIndex ? Color(hex: 0x0b1c1a) : MacSkin.textPrimary)

                            Spacer()

                            if index == selectedIndex {
                                Text("SELECTED")
                                    .font(.system(size: 9, weight: .heavy, design: .monospaced))
                                    .foregroundStyle(Color(hex: 0x0b1c1a).opacity(0.72))
                            }
                        }
                        .padding(.horizontal, 10)
                        .frame(height: 30)
                        .background(
                            RoundedRectangle(cornerRadius: 3)
                                .fill(index == selectedIndex
                                      ? LinearGradient(colors: [Color(hex: 0x32e6c8), Color(hex: 0x15937f)], startPoint: .top, endPoint: .bottom)
                                      : LinearGradient(colors: [Color(hex: 0x171812), Color(hex: 0x0c0c09)], startPoint: .top, endPoint: .bottom))
                        )
                        .overlay(
                            RoundedRectangle(cornerRadius: 3)
                                .stroke(index == selectedIndex ? Color(hex: 0x8df8ea).opacity(0.7) : Color(hex: 0x403728), lineWidth: 1)
                        )
                    }
                    .buttonStyle(.plain)
                }
            }
            .padding(.horizontal, 8)
            .padding(.bottom, 8)
        }
        .background(
            RoundedRectangle(cornerRadius: 5)
                .fill(LinearGradient(colors: [Color(hex: 0x27261f), Color(hex: 0x10100c)], startPoint: .top, endPoint: .bottom))
        )
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(Color(hex: 0x3edcca).opacity(0.68), lineWidth: 1.5))
        .shadow(color: Color.black.opacity(0.55), radius: 12, x: 0, y: 8)
    }
}

private struct PerformanceParameterControl: View {
    let title: String
    let value: Int
    let range: ClosedRange<Int>
    let valueText: String
    let onChange: (Int) -> Void

    var body: some View {
        HStack(spacing: 8) {
            Text(title)
                .font(.system(size: 12, weight: .bold))
                .foregroundStyle(MacSkin.textPrimary)
                .frame(width: 50, alignment: .trailing)
                .lineLimit(1)
                .minimumScaleFactor(0.7)

            PerformanceSlider(value: value, range: range, onChange: onChange)
                .frame(width: 120, height: 24)

            Text(valueText)
                .font(.system(size: 14, weight: .bold, design: .monospaced))
                .foregroundStyle(MacSkin.textPrimary)
                .frame(width: 44, height: 22)
                .background(Rectangle().fill(Color(hex: 0x11110f)))
                .overlay(Rectangle().stroke(MacSkin.textMuted.opacity(0.8), lineWidth: 1))
                .lineLimit(1)
                .minimumScaleFactor(0.65)
        }
        .frame(height: 24)
    }
}

private struct PerformanceSlider: View {
    let value: Int
    let range: ClosedRange<Int>
    let onChange: (Int) -> Void

    var body: some View {
        GeometryReader { proxy in
            let trackWidth = max(1, proxy.size.width - 18)
            let normalized = CGFloat(value - range.lowerBound) / CGFloat(max(1, range.upperBound - range.lowerBound))
            let thumbX = 9 + trackWidth * normalized

            ZStack(alignment: .leading) {
                RoundedRectangle(cornerRadius: 3)
                    .fill(Color(hex: 0x15272a))
                    .frame(width: trackWidth, height: 6)
                    .position(x: proxy.size.width / 2, y: proxy.size.height / 2)

                Circle()
                    .fill(Color(hex: 0x45aeda))
                    .frame(width: 18, height: 18)
                    .shadow(color: .black.opacity(0.5), radius: 1, x: 0, y: 1)
                    .position(x: thumbX, y: proxy.size.height / 2)
            }
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { gesture in
                        let x = min(max(gesture.location.x, 9), proxy.size.width - 9)
                        let ratio = (x - 9) / max(1, trackWidth)
                        let raw = CGFloat(range.lowerBound) + ratio * CGFloat(range.upperBound - range.lowerBound)
                        onChange(Int(raw.rounded()))
                    }
            )
        }
    }
}

private struct ParameterKnob: View {
    let title: String
    let value: Int
    let range: ClosedRange<Int>
    var defaultValue: Int? = nil
    let action: (Int) -> Void

    var body: some View {
        VStack(spacing: 2) {
            Text(title)
                .font(.system(size: 8, weight: .bold))
                .foregroundStyle(MacSkin.textPrimary)
                .lineLimit(1)
                .minimumScaleFactor(0.55)
                .frame(height: 10)

            RotaryKnob(value: value, range: range, defaultValue: defaultValue, action: action)
                .frame(width: 38, height: 38)

            Text("\(value)")
                .font(.system(size: 13, weight: .bold, design: .monospaced))
                .foregroundStyle(MacSkin.valueText)
                .frame(width: 48, height: 17)
                .background(Rectangle().fill(Color(hex: 0x050504)))
                .overlay(Rectangle().stroke(Color(hex: 0x4b3e23), lineWidth: 1))
        }
        .frame(width: 54, height: 70)
    }
}

private struct RotaryKnob: View {
    let value: Int
    let range: ClosedRange<Int>
    let defaultValue: Int?
    let action: (Int) -> Void
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
                        action(max(range.lowerBound, min(range.upperBound, Int(raw.rounded()))))
                    }
                    .onEnded { _ in
                        dragStartValue = nil
                        dragStartLocation = nil
                    }
            )
            .simultaneousGesture(
                TapGesture(count: 2)
                    .onEnded {
                        action(clampedDefaultValue)
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
        let start = Angle.degrees(135)
        let sweep = Angle.degrees(270)
        let activeEnd = Angle.degrees(135 + 270 * Double(normalized))

        for index in 0..<31 {
            let ratio = Double(index) / 30.0
            let angle = Double.pi * (135 + 270 * ratio) / 180.0
            let dotCenter = CGPoint(
                x: center.x + cos(angle) * radius,
                y: center.y + sin(angle) * radius
            )
            let color = ratio <= Double(normalized) ? MacSkin.teal : Color(hex: 0x4c6b68).opacity(0.65)
            context.fill(Path(ellipseIn: CGRect(x: dotCenter.x - 1.15, y: dotCenter.y - 1.15, width: 2.3, height: 2.3)),
                         with: .color(color))
        }

        let knobRect = CGRect(x: center.x - radius * 0.58, y: center.y - radius * 0.58, width: radius * 1.16, height: radius * 1.16)
        context.fill(Path(ellipseIn: knobRect.offsetBy(dx: 0, dy: 2)), with: .color(Color.black.opacity(0.55)))
        context.fill(Path(ellipseIn: knobRect), with: .linearGradient(
            Gradient(colors: [Color(hex: 0x333532), Color(hex: 0x060706)]),
            startPoint: CGPoint(x: knobRect.midX, y: knobRect.minY),
            endPoint: CGPoint(x: knobRect.midX, y: knobRect.maxY)
        ))
        context.stroke(Path(ellipseIn: knobRect), with: .color(Color.black.opacity(0.9)), lineWidth: 1.2)

        let pointerAngle = CGFloat((135 + 270 * Double(normalized)) * Double.pi / 180.0)
        let pointer = Path { path in
            path.move(to: center)
            path.addLine(to: CGPoint(
                x: center.x + cos(pointerAngle) * radius * 0.44,
                y: center.y + sin(pointerAngle) * radius * 0.44
            ))
        }
        context.stroke(pointer, with: .color(Color(hex: 0xc7c3ac)), lineWidth: 2.4)

        let inner = CGRect(x: center.x - radius * 0.25, y: center.y - radius * 0.25, width: radius * 0.5, height: radius * 0.5)
        context.fill(Path(ellipseIn: inner), with: .color(Color.black.opacity(0.35)))

        _ = start
        _ = sweep
        _ = activeEnd
    }
}

private struct StepperValue: View {
    let title: String
    let value: Int
    let rangeText: String
    let step: (Int) -> Void

    var body: some View {
        HStack(spacing: 2) {
            Text("\(title) \(value)")
                .font(.system(size: 11, weight: .bold))
                .foregroundStyle(MacSkin.textPrimary)
                .lineLimit(1)
                .minimumScaleFactor(0.65)
                .frame(maxWidth: .infinity, alignment: .leading)

            Button {
                step(-1)
            } label: {
                Text("-")
                    .font(.system(size: 12, weight: .bold))
                    .frame(width: 22)
            }
            .buttonStyle(.plain)

            Button {
                step(1)
            } label: {
                Text("+")
                    .font(.system(size: 12, weight: .bold))
                    .frame(width: 22)
            }
            .buttonStyle(.plain)
        }
        .foregroundStyle(MacSkin.textPrimary)
        .padding(.horizontal, 6)
        .frame(maxHeight: .infinity)
        .background(RoundedRectangle(cornerRadius: 3).fill(Color(hex: 0x11100d)))
        .overlay(RoundedRectangle(cornerRadius: 3).stroke(Color(hex: 0x403728), lineWidth: 1))
        .accessibilityLabel("\(title) \(rangeText)")
    }
}

private struct VoiceBadge: View {
    let text: String

    init(_ text: String) {
        self.text = text
    }

    var body: some View {
        Text(text)
            .font(.system(size: 13, weight: .bold))
            .foregroundStyle(Color(hex: 0x11110d))
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(RoundedRectangle(cornerRadius: 2).fill(MacSkin.textPrimary))
    }
}

private struct LcdDisplay: View {
    let line1: String
    let line2: String
    let scopeSamples: [Float]

    var body: some View {
        GeometryReader { proxy in
            let gap: CGFloat = 8
            let textWidth = min(max(218, proxy.size.width * 0.62), 252)
            let scopeWidth = max(0, proxy.size.width - textWidth - gap)

            HStack(spacing: gap) {
                LcdTextWindow(line1: fixedLcdLine(line1), line2: fixedLcdLine(line2))
                    .frame(width: textWidth, height: proxy.size.height)

                LcdScopeView(samples: scopeSamples)
                    .frame(width: scopeWidth, height: proxy.size.height)
                    .opacity(scopeWidth > 28 ? 1 : 0)
            }
            .frame(width: proxy.size.width, height: proxy.size.height)
        }
    }

    private func fixedLcdLine(_ text: String) -> String {
        let clipped = String(text.prefix(16))
        return clipped.padding(toLength: 16, withPad: " ", startingAt: 0)
    }
}

private struct LcdTextWindow: View {
    let line1: String
    let line2: String

    var body: some View {
        Canvas { context, size in
            drawWindow(context: &context, size: size)
        }
        .accessibilityLabel("\(line1) \(line2)")
    }

    private func drawWindow(context: inout GraphicsContext, size: CGSize) {
        let window = CGRect(origin: .zero, size: size)
        context.fill(Path(roundedRect: window, cornerRadius: 4), with: .linearGradient(
            Gradient(colors: [Color(hex: 0x0b2037), Color(hex: 0x03101f)]),
            startPoint: CGPoint(x: window.midX, y: window.minY),
            endPoint: CGPoint(x: window.midX, y: window.maxY)
        ))
        context.stroke(Path(roundedRect: window.insetBy(dx: 0.5, dy: 0.5), cornerRadius: 4),
                       with: .color(Color(hex: 0x2462ad)),
                       lineWidth: 1)
        context.stroke(Path(roundedRect: window.insetBy(dx: 2, dy: 2), cornerRadius: 3),
                       with: .color(Color.white.opacity(0.08)),
                       lineWidth: 1)

        let content = window.insetBy(dx: 5, dy: 4)
        let rowsPerCharacter = 8
        let lineGap = max(1, content.height * 0.035)
        let dotPitchY = (content.height - lineGap) / CGFloat(rowsPerCharacter * 2)
        let lineHeight = dotPitchY * CGFloat(rowsPerCharacter)
        let verticalOffset = max(1, dotPitchY)
        let firstOriginY = content.midY - lineHeight - lineGap / 2 + verticalOffset
        let secondOriginY = content.midY + lineGap / 2 + verticalOffset

        drawLine(line1, context: &context, rect: CGRect(x: content.minX, y: firstOriginY, width: content.width, height: lineHeight))
        drawLine(line2, context: &context, rect: CGRect(x: content.minX, y: secondOriginY, width: content.width, height: lineHeight))

        let highlight = CGRect(x: window.minX + 4, y: window.minY + 4, width: window.width - 8, height: 11)
        context.fill(Path(roundedRect: highlight, cornerRadius: 3), with: .color(Color.white.opacity(0.10)))
        let bottomShade = CGRect(x: window.minX + 2, y: window.maxY - 10, width: window.width - 4, height: 8)
        context.fill(Path(roundedRect: bottomShade, cornerRadius: 2), with: .color(Color.black.opacity(0.10)))
    }

    private func drawLine(_ text: String, context: inout GraphicsContext, rect: CGRect) {
        let characters = Array(text.prefix(16))
        let cellColumns = max(1, characters.count * 6 - 1)
        let dotPitchX = rect.width / CGFloat(cellColumns)
        let dotPitchY = rect.height / 8
        let dotPitch = max(1, min(dotPitchX, dotPitchY))
        let dotSize = max(1, floor(dotPitch * 0.94))
        let textWidth = dotPitch * CGFloat(cellColumns)
        let origin = CGPoint(x: rect.midX - textWidth / 2, y: rect.minY)

        for (index, character) in characters.enumerated() {
            let pattern = LcdCharacterFont.pattern(for: character)
            let characterX = origin.x + CGFloat(index * 6) * dotPitch
            for row in 0..<8 {
                let rowBits = pattern[row]
                for column in 0..<5 {
                    let rect = CGRect(
                        x: characterX + CGFloat(column) * dotPitch,
                        y: origin.y + CGFloat(row) * dotPitch,
                        width: dotSize,
                        height: dotSize
                    )
                    let isOn = (rowBits & (1 << (4 - column))) != 0
                    let color = isOn ? MacSkin.lcdOn.opacity(0.96) : Color(hex: 0x164372).opacity(0.34)
                    context.fill(Path(rect), with: .color(color))
                }
            }
        }
    }
}

private enum LcdCharacterFont {
    static func pattern(for character: Character) -> [UInt8] {
        patterns[character] ?? patterns["?"] ?? blank
    }

    private static let blank: [UInt8] = [0, 0, 0, 0, 0, 0, 0, 0]

    private static let patterns: [Character: [UInt8]] = [
        " ": blank,
        "?": [0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b00000, 0b00100, 0b00000],
        "-": [0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000, 0b00000],
        ":": [0b00000, 0b00100, 0b00100, 0b00000, 0b00000, 0b00100, 0b00100, 0b00000],
        "#": [0b01010, 0b01010, 0b11111, 0b01010, 0b11111, 0b01010, 0b01010, 0b00000],
        "0": [0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110, 0b00000],
        "1": [0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110, 0b00000],
        "2": [0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111, 0b00000],
        "3": [0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110, 0b00000],
        "4": [0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010, 0b00000],
        "5": [0b11111, 0b10000, 0b10000, 0b11110, 0b00001, 0b00001, 0b11110, 0b00000],
        "6": [0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110, 0b00000],
        "7": [0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000, 0b00000],
        "8": [0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110, 0b00000],
        "9": [0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100, 0b00000],
        "A": [0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001, 0b00000],
        "B": [0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110, 0b00000],
        "C": [0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110, 0b00000],
        "D": [0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110, 0b00000],
        "E": [0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111, 0b00000],
        "F": [0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000, 0b00000],
        "G": [0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01110, 0b00000],
        "H": [0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001, 0b00000],
        "I": [0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110, 0b00000],
        "J": [0b00111, 0b00010, 0b00010, 0b00010, 0b10010, 0b10010, 0b01100, 0b00000],
        "K": [0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001, 0b00000],
        "L": [0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111, 0b00000],
        "M": [0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001, 0b00000],
        "N": [0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001, 0b00000],
        "O": [0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000],
        "P": [0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000, 0b00000],
        "Q": [0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101, 0b00000],
        "R": [0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001, 0b00000],
        "S": [0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110, 0b00000],
        "T": [0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000],
        "U": [0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000],
        "V": [0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100, 0b00000],
        "W": [0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010, 0b00000],
        "X": [0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001, 0b00000],
        "Y": [0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000],
        "Z": [0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111, 0b00000],
        "a": [0b00000, 0b00000, 0b01110, 0b00001, 0b01111, 0b10001, 0b01111, 0b00000],
        "b": [0b10000, 0b10000, 0b10110, 0b11001, 0b10001, 0b10001, 0b11110, 0b00000],
        "c": [0b00000, 0b00000, 0b01110, 0b10001, 0b10000, 0b10001, 0b01110, 0b00000],
        "d": [0b00001, 0b00001, 0b01101, 0b10011, 0b10001, 0b10001, 0b01111, 0b00000],
        "e": [0b00000, 0b00000, 0b01110, 0b10001, 0b11111, 0b10000, 0b01110, 0b00000],
        "f": [0b00110, 0b01001, 0b01000, 0b11100, 0b01000, 0b01000, 0b01000, 0b00000],
        "g": [0b00000, 0b00000, 0b01111, 0b10001, 0b01111, 0b00001, 0b01110, 0b00000],
        "h": [0b10000, 0b10000, 0b10110, 0b11001, 0b10001, 0b10001, 0b10001, 0b00000],
        "i": [0b00100, 0b00000, 0b01100, 0b00100, 0b00100, 0b00100, 0b01110, 0b00000],
        "j": [0b00010, 0b00000, 0b00110, 0b00010, 0b00010, 0b10010, 0b01100, 0b00000],
        "k": [0b10000, 0b10000, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b00000],
        "l": [0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110, 0b00000],
        "m": [0b00000, 0b00000, 0b11010, 0b10101, 0b10101, 0b10101, 0b10101, 0b00000],
        "n": [0b00000, 0b00000, 0b10110, 0b11001, 0b10001, 0b10001, 0b10001, 0b00000],
        "o": [0b00000, 0b00000, 0b01110, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000],
        "p": [0b00000, 0b00000, 0b11110, 0b10001, 0b11110, 0b10000, 0b10000, 0b00000],
        "q": [0b00000, 0b00000, 0b01111, 0b10001, 0b01111, 0b00001, 0b00001, 0b00000],
        "r": [0b00000, 0b00000, 0b10110, 0b11001, 0b10000, 0b10000, 0b10000, 0b00000],
        "s": [0b00000, 0b00000, 0b01111, 0b10000, 0b01110, 0b00001, 0b11110, 0b00000],
        "t": [0b01000, 0b01000, 0b11100, 0b01000, 0b01000, 0b01001, 0b00110, 0b00000],
        "u": [0b00000, 0b00000, 0b10001, 0b10001, 0b10001, 0b10011, 0b01101, 0b00000],
        "v": [0b00000, 0b00000, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100, 0b00000],
        "w": [0b00000, 0b00000, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010, 0b00000],
        "x": [0b00000, 0b00000, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b00000],
        "y": [0b00000, 0b00000, 0b10001, 0b10001, 0b01111, 0b00001, 0b01110, 0b00000],
        "z": [0b00000, 0b00000, 0b11111, 0b00010, 0b00100, 0b01000, 0b11111, 0b00000]
    ]
}

private struct LcdScopeView: View {
    let samples: [Float]

    var body: some View {
        Canvas { context, size in
            guard size.width > 0, size.height > 0 else { return }

            let window = CGRect(origin: .zero, size: size)
            context.fill(Path(roundedRect: window, cornerRadius: 4), with: .linearGradient(
                Gradient(colors: [Color(hex: 0x0b2037), Color(hex: 0x03101f)]),
                startPoint: CGPoint(x: window.midX, y: window.minY),
                endPoint: CGPoint(x: window.midX, y: window.maxY)
            ))
            context.stroke(Path(roundedRect: window.insetBy(dx: 0.5, dy: 0.5), cornerRadius: 4),
                           with: .color(Color(hex: 0x2462ad)),
                           lineWidth: 1)

            guard samples.count > 1 else { return }

            let rect = window.insetBy(dx: 6, dy: 6)
            let midY = rect.midY
            var path = Path()
            for index in samples.indices {
                let x = rect.minX + rect.width * CGFloat(index) / CGFloat(samples.count - 1)
                let value = max(-1, min(1, CGFloat(samples[index])))
                let y = midY - value * rect.height * 0.44
                if index == samples.startIndex {
                    path.move(to: CGPoint(x: x, y: y))
                } else {
                    path.addLine(to: CGPoint(x: x, y: y))
                }
            }

            context.stroke(path, with: .color(MacSkin.lcdOn.opacity(0.30)), lineWidth: 4.2)
            context.stroke(path, with: .color(MacSkin.lcdOn.opacity(0.95)), lineWidth: 1.7)
        }
        .accessibilityHidden(true)
    }
}

private struct DotMatrixOverlay: View {
    var body: some View {
        GeometryReader { proxy in
            let columns = 72
            let rows = 16
            let dot = min(proxy.size.width / CGFloat(columns * 2), proxy.size.height / CGFloat(rows * 2))
            Canvas { context, _ in
                for row in 0..<rows {
                    for column in 0..<columns {
                        let rect = CGRect(
                            x: CGFloat(column) * proxy.size.width / CGFloat(columns),
                            y: CGFloat(row) * proxy.size.height / CGFloat(rows),
                            width: dot,
                            height: dot
                        )
                        context.fill(Path(roundedRect: rect, cornerRadius: dot * 0.25), with: .color(MacSkin.lcdOff))
                    }
                }
            }
        }
    }
}

private struct VolumeSlider: View {
    @Binding var value: Double

    var body: some View {
        VStack(spacing: 5) {
            GeometryReader { proxy in
                Canvas { context, size in
                    drawSlider(context: &context, size: size)
                }
                .contentShape(Rectangle())
                .gesture(drag(proxy: proxy))
            }

            Text("VOLUME")
                .font(.system(size: 9, weight: .bold))
                .foregroundStyle(MacSkin.textPrimary)
                .lineLimit(1)
                .minimumScaleFactor(0.7)
                .frame(width: 54, height: 14, alignment: .center)
        }
    }

    private func drawSlider(context: inout GraphicsContext, size: CGSize) {
        guard size.width > 0, size.height > 0 else { return }

        let panel = CGRect(
            x: (size.width - min(size.width, 44)) / 2,
            y: 0,
            width: min(size.width, 44),
            height: size.height
        ).insetBy(dx: 1, dy: 0)

        context.fill(Path(roundedRect: panel.offsetBy(dx: 0, dy: 2), cornerRadius: 1.5),
                     with: .color(Color.black.opacity(0.5)))
        context.fill(Path(roundedRect: panel, cornerRadius: 1.5), with: .linearGradient(
            Gradient(colors: [Color(hex: 0x161611), Color(hex: 0x050504)]),
            startPoint: CGPoint(x: panel.midX, y: panel.minY),
            endPoint: CGPoint(x: panel.midX, y: panel.maxY)
        ))
        context.stroke(Path(roundedRect: panel, cornerRadius: 1.5), with: .color(Color.black.opacity(0.95)), lineWidth: 1.6)
        context.stroke(Path(roundedRect: panel.insetBy(dx: 2, dy: 2), cornerRadius: 1),
                       with: .color(Color.white.opacity(0.06)), lineWidth: 1)

        let maxText = context.resolve(
            Text("MAX")
                .font(.system(size: 8, weight: .heavy))
                .foregroundColor(MacSkin.textPrimary)
        )
        context.draw(maxText, at: CGPoint(x: panel.maxX - 12, y: panel.minY + 9), anchor: .center)

        let minText = context.resolve(
            Text("MIN")
                .font(.system(size: 8, weight: .heavy))
                .foregroundColor(MacSkin.textPrimary)
        )
        context.draw(minText, at: CGPoint(x: panel.maxX - 12, y: panel.maxY - 9), anchor: .center)

        let track = CGRect(
            x: panel.minX + 14,
            y: panel.minY + 26,
            width: 6,
            height: max(1, panel.height - 50)
        )
        context.fill(Path(roundedRect: track.offsetBy(dx: 0, dy: 1), cornerRadius: 3),
                     with: .color(Color.black.opacity(0.6)))
        context.fill(Path(roundedRect: track, cornerRadius: 3),
                     with: .color(Color(hex: 0x050608)))
        context.stroke(Path(roundedRect: track, cornerRadius: 3),
                       with: .color(Color(hex: 0x161b1d)), lineWidth: 1)

        let tickCount = 8
        for index in 0..<tickCount {
            let ratio = CGFloat(index) / CGFloat(max(1, tickCount - 1))
            let y = track.maxY - ratio * track.height
            let tick = Path { path in
                path.move(to: CGPoint(x: panel.minX + 27, y: y))
                path.addLine(to: CGPoint(x: panel.maxX - 8, y: y))
            }
            context.stroke(tick, with: .color(Color(hex: 0xa8afb3).opacity(0.58)), lineWidth: 1)
        }

        let normalized = CGFloat(max(0, min(1, value)))
        let y = track.maxY - normalized * track.height
        let handle = CGRect(
            x: panel.minX + 3,
            y: y - 4.5,
            width: panel.width - 6,
            height: 9
        )
        context.fill(Path(roundedRect: handle.offsetBy(dx: 0, dy: 1.5), cornerRadius: 1.2),
                     with: .color(Color.black.opacity(0.52)))
        context.fill(Path(roundedRect: handle, cornerRadius: 1.2), with: .linearGradient(
            Gradient(colors: [Color(hex: 0xc6d3d5), Color(hex: 0x79888b)]),
            startPoint: CGPoint(x: handle.midX, y: handle.minY),
            endPoint: CGPoint(x: handle.midX, y: handle.maxY)
        ))
        context.stroke(Path(roundedRect: handle, cornerRadius: 1.2), with: .color(Color(hex: 0x273033)), lineWidth: 1)
        context.stroke(Path(CGRect(x: handle.minX + 2, y: handle.minY + 1, width: handle.width - 4, height: 1)),
                       with: .color(Color.white.opacity(0.32)), lineWidth: 1)
    }

    private func drag(proxy: GeometryProxy) -> some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { gesture in
                let y = min(max(gesture.location.y, 10), proxy.size.height - 10)
                value = Double(1.0 - (y - 10) / max(1, proxy.size.height - 20))
            }
    }
}

private struct WheelFader: View {
    let title: String
    @Binding var value: Double
    let range: ClosedRange<Double>
    var resetOnRelease = false

    var body: some View {
        VStack(spacing: 5) {
            GeometryReader { proxy in
                Canvas { context, size in
                    drawWheel(context: &context, size: size)
                }
                .contentShape(Rectangle())
                .gesture(drag(proxy: proxy))
            }

            Text(title)
                .font(.system(size: 9, weight: .bold))
                .foregroundStyle(MacSkin.textPrimary)
                .lineLimit(1)
                .minimumScaleFactor(0.72)
                .frame(width: 64, height: 14, alignment: .center)
        }
    }

    private func drawWheel(context: inout GraphicsContext, size: CGSize) {
        guard size.width > 0, size.height > 0 else { return }

        let slotWidth = min(size.width, 42)
        let slot = CGRect(x: (size.width - slotWidth) / 2,
                          y: 0,
                          width: slotWidth,
                          height: size.height)
            .insetBy(dx: 1, dy: 0)

        context.fill(Path(roundedRect: slot.offsetBy(dx: 0, dy: 2), cornerRadius: 3),
                     with: .color(Color.black.opacity(0.48)))
        context.fill(Path(roundedRect: slot, cornerRadius: 3), with: .linearGradient(
            Gradient(colors: [Color(hex: 0x1c2628), Color(hex: 0x050708)]),
            startPoint: CGPoint(x: slot.midX, y: slot.minY),
            endPoint: CGPoint(x: slot.midX, y: slot.maxY)
        ))
        context.stroke(Path(roundedRect: slot, cornerRadius: 3), with: .color(Color(hex: 0x020405)), lineWidth: 1.8)
        context.stroke(Path(roundedRect: slot.insetBy(dx: 2, dy: 2), cornerRadius: 2),
                       with: .color(Color.white.opacity(0.08)), lineWidth: 1)

        let wheel = slot.insetBy(dx: 7, dy: 6)
        context.fill(Path(roundedRect: wheel, cornerRadius: 2), with: .linearGradient(
            Gradient(colors: [Color(hex: 0x1e2a2d), Color(hex: 0x050607)]),
            startPoint: CGPoint(x: wheel.midX, y: wheel.minY),
            endPoint: CGPoint(x: wheel.midX, y: wheel.maxY)
        ))
        context.fill(Path(CGRect(x: wheel.minX, y: wheel.minY, width: 3, height: wheel.height)),
                     with: .color(Color(hex: 0x07090a)))
        context.fill(Path(CGRect(x: wheel.maxX - 3, y: wheel.minY, width: 3, height: wheel.height)),
                     with: .color(Color(hex: 0x07090a)))

        let normalized = normalizedValue
        let ribSpacing: CGFloat = 3.25
        let ribPhase = ((1 - normalized) * 42).truncatingRemainder(dividingBy: ribSpacing)
        let ribCount = Int(wheel.height / ribSpacing) + 4
        for index in -2..<ribCount {
            let y = wheel.minY + 2 + ribPhase + CGFloat(index) * ribSpacing
            if y < wheel.minY + 2 || y > wheel.maxY - 2 {
                continue
            }

            let curve = 1 - abs((y - wheel.midY) / max(1, wheel.height * 0.5))
            let inset = 3 + (1 - curve) * 2.2
            let alpha = 0.22 + curve * 0.28
            let line = Path { path in
                path.move(to: CGPoint(x: wheel.minX + inset, y: y))
                path.addLine(to: CGPoint(x: wheel.maxX - inset, y: y))
            }
            let shadowLine = Path { path in
                path.move(to: CGPoint(x: wheel.minX + inset, y: y + 1))
                path.addLine(to: CGPoint(x: wheel.maxX - inset, y: y + 1))
            }
            context.stroke(line, with: .color(Color(hex: 0x9aa7aa).opacity(alpha)), lineWidth: 1)
            context.stroke(shadowLine, with: .color(Color(hex: 0x010202).opacity(0.62)), lineWidth: 1)
        }

        let topGlow = CGRect(x: wheel.minX, y: wheel.minY, width: wheel.width, height: wheel.height * 0.32)
        context.fill(Path(roundedRect: topGlow, cornerRadius: 2), with: .linearGradient(
            Gradient(colors: [Color.white.opacity(0.12), Color.white.opacity(0)]),
            startPoint: CGPoint(x: topGlow.midX, y: topGlow.minY),
            endPoint: CGPoint(x: topGlow.midX, y: topGlow.maxY)
        ))

        let bottomShade = CGRect(x: wheel.minX, y: wheel.minY + wheel.height * 0.48, width: wheel.width, height: wheel.height * 0.52)
        context.fill(Path(roundedRect: bottomShade, cornerRadius: 2), with: .linearGradient(
            Gradient(colors: [Color.black.opacity(0), Color.black.opacity(0.46)]),
            startPoint: CGPoint(x: bottomShade.midX, y: bottomShade.minY),
            endPoint: CGPoint(x: bottomShade.midX, y: bottomShade.maxY)
        ))
        let lowerMask = CGRect(x: wheel.minX, y: wheel.minY + wheel.height * 0.55, width: wheel.width, height: wheel.height * 0.45)
        context.fill(Path(roundedRect: lowerMask, cornerRadius: 2), with: .color(Color.black.opacity(0.22)))

        context.stroke(Path(roundedRect: wheel, cornerRadius: 2), with: .color(.black), lineWidth: 1.4)

        let valueY = min(max(wheel.minY + 4, wheel.maxY - wheel.height * normalized), wheel.maxY - 4)
        let indicator = CGRect(x: wheel.minX + 3,
                               y: valueY - 1.5,
                               width: wheel.width - 6,
                               height: 3)
        context.fill(Path(roundedRect: indicator.insetBy(dx: -1, dy: -1.5), cornerRadius: 1),
                     with: .color(MacSkin.teal.opacity(0.28)))
        context.fill(Path(roundedRect: indicator, cornerRadius: 1), with: .color(MacSkin.teal))

        let highlight = Path { path in
            path.move(to: CGPoint(x: wheel.minX + 5, y: wheel.minY + 2))
            path.addLine(to: CGPoint(x: wheel.maxX - 5, y: wheel.minY + 2))
        }
        context.stroke(highlight, with: .color(Color.white.opacity(0.06)), lineWidth: 1)
    }

    private func drag(proxy: GeometryProxy) -> some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { gesture in
                let y = min(max(gesture.location.y, 12), proxy.size.height - 12)
                let normalized = 1.0 - (y - 12) / max(1, proxy.size.height - 24)
                value = range.lowerBound + Double(normalized) * (range.upperBound - range.lowerBound)
            }
            .onEnded { _ in
                if resetOnRelease {
                    value = 0
                }
            }
    }

    private var normalizedValue: CGFloat {
        CGFloat((value - range.lowerBound) / max(0.0001, range.upperBound - range.lowerBound))
    }
}
