import SwiftUI

struct LibraryView: View {
    @EnvironmentObject private var synth: MobileSynthModel

    var body: some View {
        NavigationStack {
            List {
                Section("Banks") {
                    ForEach(0..<8, id: \.self) { bank in
                        NavigationLink("Bank \(bank + 1)") {
                            VoiceListView(bank: bank)
                        }
                    }
                }

                Section("Import / Export") {
                    Button("Import SysEx Bank") {}
                    Button("Export Current Bank") {}
                }
            }
            .navigationTitle("Library")
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button("Play") {
                        synth.screen = .play
                    }
                }
            }
        }
    }
}

private struct VoiceListView: View {
    @EnvironmentObject private var synth: MobileSynthModel
    let bank: Int

    var body: some View {
        List {
            ForEach(0..<32, id: \.self) { voice in
                Button {
                    synth.selectVoice(bank: bank, voice: voice)
                    synth.screen = .play
                } label: {
                    HStack {
                        Text(String(format: "%02d", voice + 1))
                            .font(.body.monospacedDigit())
                            .foregroundStyle(.secondary)
                        Text(bank == synth.bankIndex && voice == synth.voiceIndex ? synth.voiceName : "Voice")
                        Spacer()
                    }
                }
            }
        }
        .navigationTitle("Bank \(bank + 1)")
    }
}
