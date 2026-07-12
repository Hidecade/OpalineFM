import SwiftUI

struct MobileKeyboardView: View {
    @EnvironmentObject private var synth: MobileSynthModel

    let baseNote: Int
    let noteCount: Int

    var body: some View {
        GeometryReader { proxy in
            let whiteNotes = (0..<noteCount).filter { !isBlack(note: baseNote + $0) }
            ZStack(alignment: .topLeading) {
                HStack(spacing: 1) {
                    ForEach(whiteNotes, id: \.self) { offset in
                        key(note: baseNote + offset, isBlack: false)
                    }
                }

                HStack(spacing: 0) {
                    ForEach(0..<noteCount, id: \.self) { offset in
                        let note = baseNote + offset
                        if isBlack(note: note) {
                            key(note: note, isBlack: true)
                                .frame(width: proxy.size.width / CGFloat(max(1, whiteNotes.count)) * 0.64,
                                       height: proxy.size.height * 0.62)
                                .offset(x: blackKeyOffset(for: offset, width: proxy.size.width / CGFloat(max(1, whiteNotes.count))))
                        } else {
                            Color.clear
                                .frame(width: proxy.size.width / CGFloat(max(1, whiteNotes.count)))
                        }
                    }
                }
            }
        }
    }

    private func key(note: Int, isBlack: Bool) -> some View {
        Rectangle()
            .fill(isBlack ? Color.black : Color.white)
            .overlay(Rectangle().stroke(Color.gray.opacity(0.5), lineWidth: 1))
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in synth.noteOn(note) }
                    .onEnded { _ in synth.noteOff(note) }
            )
            .accessibilityLabel("MIDI note \(note)")
    }

    private func isBlack(note: Int) -> Bool {
        [1, 3, 6, 8, 10].contains(note % 12)
    }

    private func blackKeyOffset(for offset: Int, width: CGFloat) -> CGFloat {
        let semitone = (baseNote + offset) % 12
        switch semitone {
        case 1, 6: return -width * 0.34
        case 3, 8, 10: return -width * 0.24
        default: return 0
        }
    }
}
