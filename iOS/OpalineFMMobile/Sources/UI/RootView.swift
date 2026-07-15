import SwiftUI

struct RootView: View {
    @EnvironmentObject private var synth: MobileSynthModel

    var body: some View {
        switch synth.screen {
        case .play:
            PlayView()
                .ignoresSafeArea(.container, edges: .horizontal)
        case .edit:
            EditView()
        case .library:
            LibraryView()
        }
    }
}
