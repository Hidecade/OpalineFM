import SwiftUI

@main
struct OpalineFMMobileApp: App {
    @StateObject private var synth = MobileSynthModel()
    @Environment(\.scenePhase) private var scenePhase

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(synth)
                .onChange(of: scenePhase) { phase in
                    switch phase {
                    case .active:
                        synth.applicationDidBecomeActive()
                    case .background:
                        synth.applicationDidEnterBackground()
                    case .inactive:
                        break
                    @unknown default:
                        break
                    }
                }
        }
    }
}
