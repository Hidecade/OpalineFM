import SwiftUI
import UIKit

private let keyboardAllWhiteNotes: [Int] = (21...108).filter { !keyboardIsBlack($0) }
private let keyboardBlackKeys: [KeyboardBlackKey] = (21...108)
    .filter { keyboardIsBlack($0) }
    .map { note in
        KeyboardBlackKey(
            note: note,
            whiteBefore: keyboardAllWhiteNotes.firstIndex(where: { $0 >= note }) ?? keyboardAllWhiteNotes.count
        )
    }

private func keyboardIsBlack(_ note: Int) -> Bool {
    switch note % 12 {
    case 1, 3, 6, 8, 10:
        return true
    default:
        return false
    }
}

struct MobileKeyboardView: View, Animatable {
    @EnvironmentObject private var synth: MobileSynthModel
    @State private var activeNotes: [Int: Int] = [:]

    @Binding var baseNote: Int
    let visibleWhiteKeyCount: Int
    var scrollWhiteIndex: CGFloat

    var animatableData: CGFloat {
        get { scrollWhiteIndex }
        set { scrollWhiteIndex = newValue }
    }

    var body: some View {
        ZStack {
            Canvas { context, size in
                drawKeyboard(context: &context, size: size)
            }

            KeyboardTouchSurface(
                synth: synth,
                baseNote: baseNote,
                activeNotes: $activeNotes,
                visibleWhiteKeyCount: visibleWhiteKeyCount,
                scrollWhiteIndex: scrollWhiteIndex
            )
        }
        .accessibilityLabel("Mobile keyboard")
    }

    private var displayedActiveNotes: [Int: Int] {
        activeNotes.merging(synth.externalActiveNotes) { touchVelocity, midiVelocity in
            max(touchVelocity, midiVelocity)
        }
    }

    private func drawKeyboard(context: inout GraphicsContext, size: CGSize) {
        guard size.width > 0, size.height > 0 else { return }
        let activeNotes = displayedActiveNotes

        let baseArea = CGRect(origin: .zero, size: size).insetBy(dx: 0, dy: 0)
        context.fill(Path(baseArea), with: .linearGradient(
            Gradient(colors: [Color(hex: 0x1b1a16), Color(hex: 0x050505)]),
            startPoint: CGPoint(x: baseArea.midX, y: baseArea.minY),
            endPoint: CGPoint(x: baseArea.midX, y: baseArea.maxY)
        ))

        let keyArea = CGRect(
            x: baseArea.minX,
            y: baseArea.minY,
            width: max(1, baseArea.width),
            height: max(1, baseArea.height - 3)
        )
        let whiteWidth = keyArea.width / CGFloat(max(1, visibleWhiteKeyCount))
        let blackHeight = keyArea.height * 0.62
        let blackWidth = whiteWidth * 0.58
        let scrollX = -scrollWhiteIndex * whiteWidth
        let firstVisibleWhite = max(0, Int(floor(scrollWhiteIndex)) - 1)
        let lastVisibleWhite = min(keyboardAllWhiteNotes.count - 1, Int(ceil(scrollWhiteIndex)) + visibleWhiteKeyCount + 1)

        for index in firstVisibleWhite...lastVisibleWhite {
            let note = keyboardAllWhiteNotes[index]
            let noteVelocity = activeNotes[note] ?? 0
            let held = noteVelocity > 0
            let rect = CGRect(x: keyArea.minX + scrollX + CGFloat(index) * whiteWidth + 0.45,
                              y: keyArea.minY,
                              width: max(1, whiteWidth - 0.9),
                              height: max(1, keyArea.height - 2))
            guard rect.maxX >= keyArea.minX - whiteWidth, rect.minX <= keyArea.maxX + whiteWidth else {
                continue
            }
            context.fill(Path(rect), with: .linearGradient(
                Gradient(colors: held
                         ? [Color(hex: 0xb2efff), Color(hex: 0x70cde7)]
                         : [Color(hex: 0xf4eee1), Color(hex: 0xd8cdb7)]),
                startPoint: CGPoint(x: rect.midX, y: rect.minY),
                endPoint: CGPoint(x: rect.midX, y: rect.maxY)
            ))
            context.stroke(Path(roundedRect: rect, cornerRadius: 1.7), with: .color(Color(hex: 0x5a5143).opacity(0.74)), lineWidth: 1)
            context.fill(Path(CGRect(x: rect.minX + 1.2, y: rect.minY, width: max(1, rect.width - 2.4), height: 5)),
                         with: .color(Color.white.opacity(held ? 0.18 : 0.25)))
            context.fill(Path(CGRect(x: rect.minX, y: rect.maxY - 4, width: rect.width, height: 4)),
                         with: .color(Color.black.opacity(0.20)))

            if held {
                drawVelocity(noteVelocity, in: rect, context: &context, blackKey: false)
            }

            if note % 12 == 0 {
                drawCLabel(note: note, in: rect, context: &context, held: held)
            }
        }

        for key in keyboardBlackKeys {
            guard key.whiteBefore >= firstVisibleWhite - 1, key.whiteBefore <= lastVisibleWhite + 1 else {
                continue
            }
            let noteVelocity = activeNotes[key.note] ?? 0
            let held = noteVelocity > 0
            let x = keyArea.minX + scrollX + CGFloat(key.whiteBefore) * whiteWidth - blackWidth * 0.5
            let rect = CGRect(x: x, y: keyArea.minY - 1, width: blackWidth, height: blackHeight).insetBy(dx: 1.2, dy: 0)
            guard rect.maxX >= keyArea.minX - blackWidth, rect.minX <= keyArea.maxX + blackWidth else {
                continue
            }
            let path = Path(roundedRect: rect, cornerRadius: 1)

            context.fill(
                Path(roundedRect: rect.offsetBy(dx: 0, dy: 3), cornerRadius: 1),
                with: .color(Color.black.opacity(0.28))
            )
            context.fill(path, with: .linearGradient(
                Gradient(colors: held
                         ? [Color(hex: 0x284650), Color(hex: 0x0d8aa8)]
                         : [Color(hex: 0x20201c), Color(hex: 0x050504)]),
                startPoint: CGPoint(x: rect.midX, y: rect.minY),
                endPoint: CGPoint(x: rect.midX, y: rect.maxY)
            ))

            context.fill(Path(roundedRect: rect.insetBy(dx: 3, dy: 3).withHeight(rect.height * 0.18), cornerRadius: 1),
                         with: .color(Color.white.opacity(held ? 0.16 : 0.08)))
            context.fill(Path(CGRect(x: rect.minX, y: rect.maxY - 5, width: rect.width, height: 5)),
                         with: .color(Color.black.opacity(0.35)))
            context.stroke(path, with: .color(Color.black.opacity(0.95)), lineWidth: 1.2)

            if held {
                drawVelocity(noteVelocity, in: rect, context: &context, blackKey: true)
            }
        }

        var sideAndBottom = Path()
        sideAndBottom.move(to: CGPoint(x: baseArea.minX, y: baseArea.minY))
        sideAndBottom.addLine(to: CGPoint(x: baseArea.minX, y: baseArea.maxY))
        sideAndBottom.addLine(to: CGPoint(x: baseArea.maxX, y: baseArea.maxY))
        sideAndBottom.addLine(to: CGPoint(x: baseArea.maxX, y: baseArea.minY))
        context.stroke(sideAndBottom, with: .color(Color.black.opacity(0.76)), lineWidth: 2)
    }

    private func drawVelocity(_ velocity: Int, in rect: CGRect, context: inout GraphicsContext, blackKey: Bool) {
        let width = blackKey ? CGFloat(22) : min(CGFloat(34), max(18, rect.width - 8))
        let height = blackKey ? CGFloat(17) : CGFloat(18)
        let valueBox = CGRect(
            x: rect.midX - width / 2,
            y: rect.maxY - (blackKey ? 24 : 28),
            width: width,
            height: height
        )
        context.fill(Path(roundedRect: valueBox, cornerRadius: 2), with: .color(Color(hex: 0x050606).opacity(blackKey ? 0.84 : 0.82)))
        let text = context.resolve(
            Text("\(velocity)")
                .font(.system(size: blackKey && velocity >= 100 ? 7.5 : 10.5, weight: .bold))
                .foregroundColor(Color(hex: 0xffffd52b))
        )
        context.draw(text, at: CGPoint(x: valueBox.midX, y: valueBox.midY), anchor: .center)
    }

    private func drawCLabel(note: Int, in rect: CGRect, context: inout GraphicsContext, held: Bool) {
        let octave = note / 12 - 1
        let text = context.resolve(
            Text("C\(octave)")
                .font(.system(size: min(13, max(8, rect.width * 0.28)), weight: .bold, design: .monospaced))
                .foregroundColor(held ? Color(hex: 0x0b2630).opacity(0.82) : Color(hex: 0x2c2922).opacity(0.72))
        )
        context.draw(
            text,
            at: CGPoint(x: rect.midX, y: rect.maxY - max(14, rect.height * 0.11)),
            anchor: .center
        )
    }

}

private struct KeyboardBlackKey {
    let note: Int
    let whiteBefore: Int
}

private struct KeyboardTouchSurface: UIViewRepresentable {
    let synth: MobileSynthModel
    let baseNote: Int
    @Binding var activeNotes: [Int: Int]
    let visibleWhiteKeyCount: Int
    let scrollWhiteIndex: CGFloat

    func makeUIView(context: Context) -> KeyboardTouchSurfaceView {
        let view = KeyboardTouchSurfaceView()
        view.synth = synth
        view.baseNote = baseNote
        view.visibleWhiteKeyCount = visibleWhiteKeyCount
        view.scrollWhiteIndex = scrollWhiteIndex
        view.onActiveNotesChanged = { activeNotes = $0 }
        return view
    }

    func updateUIView(_ uiView: KeyboardTouchSurfaceView, context: Context) {
        uiView.synth = synth
        uiView.baseNote = baseNote
        uiView.visibleWhiteKeyCount = visibleWhiteKeyCount
        uiView.scrollWhiteIndex = scrollWhiteIndex
        uiView.onActiveNotesChanged = { activeNotes = $0 }
    }
}

private final class KeyboardTouchSurfaceView: UIView {
    weak var synth: MobileSynthModel?
    var baseNote = 53
    var visibleWhiteKeyCount = 17
    var scrollWhiteIndex: CGFloat = 0
    var onActiveNotesChanged: (([Int: Int]) -> Void)?

    private let touchAttackSampleDelay: TimeInterval = 0.024
    private let positionVelocityWeight = 0.70
    private let downwardVelocityWeight = 0.30
    private let velocityLevels = [64, 76, 86, 94, 100]
    private var touchStates: [UITouch: KeyboardTouchState] = [:]
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
            let location = touch.location(in: self)
            let note = note(at: location)
            let velocity = velocity(at: location, note: note)
            touchStates[touch] = KeyboardTouchState(
                note: note,
                velocity: velocity,
                startLocation: location,
                startRadius: touch.majorRadius,
                startTimestamp: touch.timestamp
            )
            if let note {
                scheduleStart(for: touch, note: note, fallbackVelocity: velocity)
            }
        }
        emitActiveNotes()
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches {
            guard var state = touchStates[touch] else { continue }
            let location = touch.location(in: self)
            let newNote = note(at: location)
            let newVelocity = velocity(at: location, note: newNote)
            if state.note != newNote {
                if let note = state.note {
                    cancelPendingStart(for: touch)
                    if state.started {
                        stop(note: note)
                    }
                }
                if let newNote {
                    state = KeyboardTouchState(
                        note: newNote,
                        velocity: newVelocity,
                        startLocation: location,
                        startRadius: touch.majorRadius,
                        startTimestamp: touch.timestamp
                    )
                    touchStates[touch] = state
                    scheduleStart(for: touch, note: newNote, fallbackVelocity: newVelocity)
                    continue
                }
                state.note = newNote
            } else if let newNote, state.started {
                noteVelocities[newNote] = newVelocity
            }
            state.velocity = newVelocity
            touchStates[touch] = state
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
            guard let state = touchStates.removeValue(forKey: touch) else { continue }
            state.pendingStart?.cancel()
            if let note = state.note {
                if state.started {
                    stop(note: note)
                }
            }
        }
        emitActiveNotes()
    }

    private func scheduleStart(for touch: UITouch, note: Int, fallbackVelocity: Int) {
        var state = touchStates[touch]
        state?.pendingStart?.cancel()

        let workItem = DispatchWorkItem { [weak self, weak touch] in
            guard let self, let touch, var latestState = self.touchStates[touch] else { return }
            guard latestState.note == note, !latestState.started else { return }

            let velocity = self.attackVelocity(for: touch, note: note, state: latestState, fallbackVelocity: fallbackVelocity)
            latestState.velocity = velocity
            latestState.started = true
            latestState.pendingStart = nil
            self.touchStates[touch] = latestState
            self.start(note: note, velocity: velocity)
            self.emitActiveNotes()
        }

        state?.pendingStart = workItem
        touchStates[touch] = state
        DispatchQueue.main.asyncAfter(deadline: .now() + touchAttackSampleDelay, execute: workItem)
    }

    private func cancelPendingStart(for touch: UITouch) {
        touchStates[touch]?.pendingStart?.cancel()
        touchStates[touch]?.pendingStart = nil
    }

    private func start(note: Int, velocity: Int) {
        let count = noteCounts[note] ?? 0
        noteCounts[note] = count + 1
        noteVelocities[note] = velocity
        if count == 0 {
            synth?.noteOn(note, velocity: velocity)
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

    private func attackVelocity(for touch: UITouch, note: Int, state: KeyboardTouchState, fallbackVelocity: Int) -> Int {
        let location = touch.location(in: self)
        let positionVelocity = velocity(at: location, note: note)
        let elapsed = max(0.006, touch.timestamp - state.startTimestamp)
        let downwardDelta = max(0, location.y - state.startLocation.y)
        let downwardSpeed = downwardDelta / CGFloat(elapsed)
        let downwardVelocity = velocityFromDownwardSpeed(downwardSpeed)
        let blended = Double(positionVelocity) * positionVelocityWeight
            + Double(downwardVelocity) * downwardVelocityWeight
        let corrected = max(Double(fallbackVelocity) * 0.42, blended)
        return quantizedVelocity(fromRawValue: corrected)
    }

    private func velocityFromDownwardSpeed(_ speed: CGFloat) -> Int {
        let normalized = min(max(speed / 170.0, 0), 1)
        let curved = 1 - pow(1 - Double(normalized), 1.8)
        return quantizedVelocity(fromNormalized: curved)
    }

    private func note(at location: CGPoint) -> Int? {
        guard bounds.width > 0, bounds.height > 0 else { return nil }
        let keyArea = CGRect(x: 0, y: 0, width: max(1, bounds.width), height: max(1, bounds.height - 3))
        guard keyArea.contains(location) else { return nil }

        let whiteWidth = keyArea.width / CGFloat(max(1, visibleWhiteKeyCount))
        let blackHeight = keyArea.height * 0.62
        let blackWidth = whiteWidth * 0.58
        let contentX = location.x + scrollWhiteIndex * whiteWidth

        if location.y <= keyArea.minY + blackHeight {
            let approximateWhiteIndex = Int((contentX - keyArea.minX) / max(1, whiteWidth))
            for key in keyboardBlackKeys where abs(key.whiteBefore - approximateWhiteIndex) <= 1 {
                let x = keyArea.minX + CGFloat(key.whiteBefore) * whiteWidth - blackWidth * 0.5
                if CGRect(x: x, y: keyArea.minY, width: blackWidth, height: blackHeight)
                    .contains(CGPoint(x: contentX, y: location.y)) {
                    return key.note
                }
            }
        }

        let whiteIndex = min(max(0, Int((contentX - keyArea.minX) / max(1, whiteWidth))), keyboardAllWhiteNotes.count - 1)
        return keyboardAllWhiteNotes[whiteIndex]
    }

    private func velocity(at location: CGPoint, note: Int?) -> Int {
        guard bounds.height > 0 else { return quantizedVelocity(fromNormalized: 0.75) }
        let keyArea = CGRect(x: 0, y: 0, width: max(1, bounds.width), height: max(1, bounds.height - 3))
        let playableHeight = note.map(keyboardIsBlack) == true ? keyArea.height * 0.62 : keyArea.height
        let normalized = min(max((location.y - keyArea.minY) / max(1, playableHeight), 0), 1)
        return quantizedVelocity(fromNormalized: Double(normalized))
    }

    private func quantizedVelocity(fromNormalized normalized: Double) -> Int {
        let safeNormalized = min(max(normalized, 0), 1)
        let step = Int((safeNormalized * Double(velocityLevels.count - 1)).rounded())
        return velocityLevels[min(max(0, step), velocityLevels.count - 1)]
    }

    private func quantizedVelocity(fromRawValue rawValue: Double) -> Int {
        velocityLevels.min { lhs, rhs in
            abs(Double(lhs) - rawValue) < abs(Double(rhs) - rawValue)
        } ?? velocityLevels[0]
    }

}

private struct KeyboardTouchState {
    var note: Int?
    var velocity: Int
    var startLocation: CGPoint
    var startRadius: CGFloat
    var startTimestamp: TimeInterval
    var started = false
    var pendingStart: DispatchWorkItem?
}

private extension CGRect {
    func withHeight(_ height: CGFloat) -> CGRect {
        CGRect(x: minX, y: minY, width: width, height: height)
    }
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
