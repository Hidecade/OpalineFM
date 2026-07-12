import SwiftUI

struct WheelView: View {
    let title: String
    @Binding var value: Double
    let range: ClosedRange<Double>

    var body: some View {
        VStack(spacing: 8) {
            Text(title)
                .font(.caption.bold())
                .foregroundStyle(.secondary)

            GeometryReader { proxy in
                ZStack(alignment: .bottom) {
                    RoundedRectangle(cornerRadius: 8)
                        .fill(Color(.secondarySystemBackground))

                    RoundedRectangle(cornerRadius: 8)
                        .fill(Color.accentColor.opacity(0.7))
                        .frame(height: fillHeight(in: proxy.size.height))
                }
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { gesture in
                            let normalized = 1.0 - min(1.0, max(0.0, gesture.location.y / max(1, proxy.size.height)))
                            value = range.lowerBound + normalized * (range.upperBound - range.lowerBound)
                        }
                )
            }

            Text(String(format: "%.2f", value))
                .font(.caption.monospacedDigit())
                .foregroundStyle(.secondary)
        }
    }

    private func fillHeight(in height: CGFloat) -> CGFloat {
        let normalized = (value - range.lowerBound) / max(0.0001, range.upperBound - range.lowerBound)
        return height * CGFloat(max(0, min(1, normalized)))
    }
}
