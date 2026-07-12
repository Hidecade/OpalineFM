import SwiftUI

struct EditView: View {
    private let operatorNames = ["OP1", "OP2", "OP3", "OP4"]

    var body: some View {
        NavigationStack {
            List {
                Section("Voice") {
                    NavigationLink("Algorithm and feedback") {
                        PlaceholderEditorPage(title: "Algorithm")
                    }
                    NavigationLink("Pitch envelope") {
                        PlaceholderEditorPage(title: "Pitch Envelope")
                    }
                    NavigationLink("LFO") {
                        PlaceholderEditorPage(title: "LFO")
                    }
                }

                Section("Operators") {
                    ForEach(operatorNames, id: \.self) { name in
                        NavigationLink(name) {
                            PlaceholderEditorPage(title: name)
                        }
                    }
                }

                Section("Effects") {
                    NavigationLink("Reverb, delay, chorus, tone") {
                        PlaceholderEditorPage(title: "Effects")
                    }
                }
            }
            .navigationTitle("Edit")
        }
    }
}

private struct PlaceholderEditorPage: View {
    let title: String

    var body: some View {
        Form {
            Section(title) {
                Text("Parameter controls will be wired to the shared OpalinePatch model here.")
                    .foregroundStyle(.secondary)
            }
        }
        .navigationTitle(title)
    }
}
