import SwiftUI

struct RootView: View {
    @EnvironmentObject private var synth: MobileSynthModel

    var body: some View {
        VStack(spacing: 0) {
            switch synth.screen {
            case .play:
                PlayView()
            case .edit:
                EditView()
            case .library:
                LibraryView()
            }

            Divider()

            HStack(spacing: 0) {
                tabButton("Play", screen: .play)
                tabButton("Edit", screen: .edit)
                tabButton("Library", screen: .library)
            }
            .background(Color(.secondarySystemBackground))
        }
    }

    private func tabButton(_ title: String, screen: MobileSynthModel.Screen) -> some View {
        Button {
            synth.screen = screen
        } label: {
            Text(title)
                .font(.headline)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 14)
                .foregroundStyle(synth.screen == screen ? .primary : .secondary)
        }
        .buttonStyle(.plain)
    }
}
