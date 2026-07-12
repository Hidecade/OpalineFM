import SwiftUI

struct PlayView: View {
    @EnvironmentObject private var synth: MobileSynthModel

    var body: some View {
        VStack(spacing: 14) {
            header
            performancePanel
            Spacer(minLength: 0)
            wheelRow
            MobileKeyboardView(baseNote: 48, noteCount: 25)
                .environmentObject(synth)
                .frame(height: 168)
        }
        .padding()
        .background(Color(.systemBackground))
    }

    private var header: some View {
        HStack {
            VStack(alignment: .leading, spacing: 4) {
                Text("Opaline FM")
                    .font(.title2.bold())
                Text(synth.voiceName)
                    .font(.headline)
                    .foregroundStyle(.secondary)
                Text(synth.audioStatus)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Spacer()
            Button {
                synth.previousVoice()
            } label: {
                Image(systemName: "chevron.left")
                    .font(.title3)
                    .frame(width: 44, height: 44)
            }
            Button {
                synth.nextVoice()
            } label: {
                Image(systemName: "chevron.right")
                    .font(.title3)
                    .frame(width: 44, height: 44)
            }
        }
    }

    private var performancePanel: some View {
        VStack(spacing: 12) {
            HStack {
                Picker("Mode", selection: .constant("Single")) {
                    Text("Single").tag("Single")
                    Text("Dual").tag("Dual")
                    Text("Split").tag("Split")
                }
                .pickerStyle(.segmented)

                Toggle("Mono", isOn: Binding(
                    get: { synth.isMono },
                    set: { synth.setMono($0) }
                ))
                .toggleStyle(.switch)
            }

            HStack(spacing: 12) {
                performanceMeter(title: "Bank", value: "\(synth.bankIndex + 1)")
                performanceMeter(title: "Voice", value: String(format: "%02d", synth.voiceIndex + 1))
                performanceMeter(title: "Poly", value: synth.isMono ? "Mono" : "Poly")
            }
        }
        .padding(14)
        .background(Color(.secondarySystemBackground))
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }

    private func performanceMeter(title: String, value: String) -> some View {
        VStack(spacing: 4) {
            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)
            Text(value)
                .font(.title3.monospacedDigit().bold())
        }
        .frame(maxWidth: .infinity)
    }

    private var wheelRow: some View {
        HStack(spacing: 18) {
            WheelView(title: "Pitch", value: Binding(
                get: { synth.pitchWheel },
                set: { synth.setPitchWheel($0) }
            ), range: -1...1)

            WheelView(title: "Mod", value: Binding(
                get: { synth.modWheel },
                set: { synth.setModWheel($0) }
            ), range: 0...1)
        }
        .frame(height: 150)
    }
}
