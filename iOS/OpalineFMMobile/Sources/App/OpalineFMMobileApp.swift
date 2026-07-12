import SwiftUI

@main
struct OpalineFMMobileApp: App {
    @StateObject private var synth = MobileSynthModel()

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(synth)
        }
    }
}
