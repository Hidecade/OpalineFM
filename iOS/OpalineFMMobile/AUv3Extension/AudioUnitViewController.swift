import AudioToolbox
import CoreAudioKit
import UIKit

public final class AudioUnitViewController: AUViewController, AUAudioUnitFactory {
    private var audioUnit: OpalineAUAudioUnit?
    private var voiceNames: [String] = []
    private var voiceIndex = 0
    private let fxButton = UIButton(type: .system)
    private let monoButton = UIButton(type: .system)
    private let portamentoButton = UIButton(type: .system)
    private let voiceButton = UIButton(type: .system)
    private let previousButton = UIButton(type: .system)
    private let nextButton = UIButton(type: .system)
    private let statusLabel = UILabel()

    public override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = UIColor(red: 0.02, green: 0.025, blue: 0.025, alpha: 1.0)

        let titleLabel = UILabel()
        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.text = "Opaline FM"
        titleLabel.textColor = UIColor(red: 0.58, green: 0.90, blue: 1.0, alpha: 1.0)
        titleLabel.font = .systemFont(ofSize: 22, weight: .bold)
        titleLabel.textAlignment = .center

        statusLabel.translatesAutoresizingMaskIntoConstraints = false
        statusLabel.text = "Single AUv3"
        statusLabel.textColor = UIColor(white: 0.78, alpha: 1.0)
        statusLabel.font = .systemFont(ofSize: 13, weight: .semibold)
        statusLabel.textAlignment = .center

        configureButton(fxButton, title: "FX ON")
        fxButton.addTarget(self, action: #selector(toggleFX), for: .touchUpInside)
        configureButton(monoButton, title: "MONO OFF")
        monoButton.addTarget(self, action: #selector(toggleMono), for: .touchUpInside)
        configureButton(portamentoButton, title: "PORTA OFF")
        portamentoButton.addTarget(self, action: #selector(stepPortamento), for: .touchUpInside)

        configureButton(previousButton, title: "-")
        configureButton(nextButton, title: "+")
        configureButton(voiceButton, title: "Voice")
        previousButton.addTarget(self, action: #selector(previousVoice), for: .touchUpInside)
        nextButton.addTarget(self, action: #selector(nextVoice), for: .touchUpInside)

        let voiceRow = UIStackView(arrangedSubviews: [previousButton, voiceButton, nextButton])
        voiceRow.translatesAutoresizingMaskIntoConstraints = false
        voiceRow.axis = .horizontal
        voiceRow.alignment = .fill
        voiceRow.spacing = 8

        let modeRow = UIStackView(arrangedSubviews: [fxButton, monoButton, portamentoButton])
        modeRow.translatesAutoresizingMaskIntoConstraints = false
        modeRow.axis = .horizontal
        modeRow.alignment = .fill
        modeRow.spacing = 8

        let stack = UIStackView(arrangedSubviews: [titleLabel, voiceRow, modeRow, statusLabel])
        stack.translatesAutoresizingMaskIntoConstraints = false
        stack.axis = .vertical
        stack.alignment = .center
        stack.spacing = 10
        view.addSubview(stack)

        NSLayoutConstraint.activate([
            stack.centerXAnchor.constraint(equalTo: view.safeAreaLayoutGuide.centerXAnchor),
            stack.centerYAnchor.constraint(equalTo: view.safeAreaLayoutGuide.centerYAnchor),
            stack.leadingAnchor.constraint(greaterThanOrEqualTo: view.safeAreaLayoutGuide.leadingAnchor, constant: 14),
            stack.trailingAnchor.constraint(lessThanOrEqualTo: view.safeAreaLayoutGuide.trailingAnchor, constant: -14),
            previousButton.widthAnchor.constraint(equalToConstant: 44),
            nextButton.widthAnchor.constraint(equalToConstant: 44),
            voiceButton.widthAnchor.constraint(equalToConstant: 220),
            voiceButton.heightAnchor.constraint(equalToConstant: 38),
            fxButton.widthAnchor.constraint(equalToConstant: 92),
            monoButton.widthAnchor.constraint(equalToConstant: 92),
            portamentoButton.widthAnchor.constraint(equalToConstant: 112),
            fxButton.heightAnchor.constraint(equalToConstant: 36)
        ])
        monoButton.heightAnchor.constraint(equalTo: fxButton.heightAnchor).isActive = true
        portamentoButton.heightAnchor.constraint(equalTo: fxButton.heightAnchor).isActive = true

        refreshFromAudioUnit()
    }

    public func createAudioUnit(with componentDescription: AudioComponentDescription) throws -> AUAudioUnit {
        let unit = try OpalineAUAudioUnit(componentDescription: componentDescription)
        audioUnit = unit
        DispatchQueue.main.async { [weak self] in
            self?.refreshFromAudioUnit()
        }
        return unit
    }

    @objc private func previousVoice() {
        stepVoice(-1)
    }

    @objc private func nextVoice() {
        stepVoice(1)
    }

    @objc private func toggleFX() {
        guard let parameter = parameter(identifier: "effectsEnabled") else { return }
        parameter.value = parameter.value >= 0.5 ? 0 : 1
        updateLabels()
    }

    @objc private func toggleMono() {
        guard let parameter = parameter(identifier: "mono") else { return }
        parameter.value = parameter.value >= 0.5 ? 0 : 1
        updateLabels()
    }

    @objc private func stepPortamento() {
        guard let parameter = parameter(identifier: "portamentoPreset") else { return }
        let nextValue = (Int(parameter.value) + 1) % 7
        parameter.value = Float(nextValue)
        updateLabels()
    }

    private func stepVoice(_ delta: Int) {
        guard !voiceNames.isEmpty else { return }
        voiceIndex = (voiceIndex + delta + voiceNames.count) % voiceNames.count
        guard let parameter = parameter(identifier: "voiceA") else { return }
        parameter.value = Float(voiceIndex)
        updateLabels()
    }

    private func refreshFromAudioUnit() {
        guard isViewLoaded else { return }
        if let voiceParameter = parameter(identifier: "voiceA") {
            let names = voiceParameter.valueStrings ?? []
            voiceNames = names.count == 32 ? names : fallbackVoiceNames()
            voiceIndex = clamp(Int(voiceParameter.value), min: 0, max: max(0, voiceNames.count - 1))
        } else {
            voiceNames = fallbackVoiceNames()
            voiceIndex = 0
        }
        updateLabels()
    }

    private func updateLabels() {
        guard isViewLoaded else { return }
        let fxOn = (parameter(identifier: "effectsEnabled")?.value ?? 1) >= 0.5
        let monoOn = (parameter(identifier: "mono")?.value ?? 0) >= 0.5
        let porta = Int(parameter(identifier: "portamentoPreset")?.value ?? 0)
        voiceButton.setTitle(voiceTitle(at: voiceIndex), for: .normal)
        fxButton.setTitle(fxOn ? "FX ON" : "FX OFF", for: .normal)
        monoButton.setTitle(monoOn ? "MONO ON" : "MONO OFF", for: .normal)
        portamentoButton.setTitle(portamentoTitle(porta), for: .normal)
        statusLabel.text = monoOn ? "Single AUv3 / Mono" : "Single AUv3 / Poly"
    }

    private func voiceTitle(at index: Int) -> String {
        guard index >= 0, index < voiceNames.count else { return "--" }
        return voiceNames[index]
    }

    private func parameter(identifier: String) -> AUParameter? {
        audioUnit?.parameterTree?.allParameters.first { $0.identifier == identifier }
    }

    private func portamentoTitle(_ value: Int) -> String {
        switch value {
        case 1: return "FULL S"
        case 2: return "FULL M"
        case 3: return "FULL L"
        case 4: return "FINGER S"
        case 5: return "FINGER M"
        case 6: return "FINGER L"
        default: return "PORTA OFF"
        }
    }

    private func configureButton(_ button: UIButton, title: String) {
        button.translatesAutoresizingMaskIntoConstraints = false
        button.setTitle(title, for: .normal)
        button.setTitleColor(UIColor(red: 0.80, green: 0.96, blue: 1.0, alpha: 1.0), for: .normal)
        button.titleLabel?.font = .systemFont(ofSize: 13, weight: .bold)
        button.backgroundColor = UIColor(red: 0.08, green: 0.10, blue: 0.10, alpha: 1.0)
        button.layer.cornerRadius = 6
        button.layer.borderWidth = 1
        button.layer.borderColor = UIColor(red: 0.20, green: 0.42, blue: 0.46, alpha: 1.0).cgColor
    }

    private func fallbackVoiceNames() -> [String] {
        (1...32).map { String(format: "%02d Voice", $0) }
    }

    private func clamp(_ value: Int, min minValue: Int, max maxValue: Int) -> Int {
        Swift.min(Swift.max(value, minValue), maxValue)
    }
}
