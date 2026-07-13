import CoreMIDI
import Foundation

struct MobileMIDIInputDevice: Identifiable, Equatable {
    let id: Int32
    let name: String
}

final class MobileMIDIInput {
    private weak var synth: MobileSynthModel?
    private var client = MIDIClientRef()
    private var inputPort = MIDIPortRef()
    private var connectedSources = Set<MIDIEndpointRef>()
    private var runningStatus: UInt8?
    private var selectedInputID: Int32?
    private var receiveChannel: Int?

    var onDevicesChanged: (([MobileMIDIInputDevice]) -> Void)?

    init(synth: MobileSynthModel) {
        self.synth = synth
        MIDIClientCreateWithBlock("Opaline FM MIDI Client" as CFString, &client) { [weak self] _ in
            DispatchQueue.main.async {
                self?.refreshDevicesAndConnections()
            }
        }
        MIDIInputPortCreateWithBlock(client, "Opaline FM MIDI Input" as CFString, &inputPort) { [weak self] packetList, _ in
            self?.handle(packetList: packetList.pointee)
        }
        refreshDevicesAndConnections()
    }

    deinit {
        if inputPort != 0 {
            MIDIPortDispose(inputPort)
        }
        if client != 0 {
            MIDIClientDispose(client)
        }
    }

    func setSelectedInput(id: Int32?) {
        selectedInputID = id
        refreshDevicesAndConnections()
    }

    func setReceiveChannel(_ channel: Int?) {
        receiveChannel = channel.map { max(1, min(16, $0)) }
    }

    func refreshDevicesAndConnections() {
        let devices = currentDevices()
        if let selectedInputID, !devices.contains(where: { $0.id == selectedInputID }) {
            self.selectedInputID = nil
        }

        disconnectAllSources()
        connectSelectedSources()
        onDevicesChanged?(devices)
    }

    private func currentDevices() -> [MobileMIDIInputDevice] {
        let sourceCount = MIDIGetNumberOfSources()
        return (0..<sourceCount).compactMap { index in
            let source = MIDIGetSource(index)
            guard source != 0 else { return nil }
            return MobileMIDIInputDevice(id: uniqueID(for: source), name: displayName(for: source))
        }
    }

    private func connectSelectedSources() {
        let sourceCount = MIDIGetNumberOfSources()
        for index in 0..<sourceCount {
            let source = MIDIGetSource(index)
            guard source != 0, !connectedSources.contains(source) else { continue }
            if let selectedInputID, uniqueID(for: source) != selectedInputID {
                continue
            }
            if MIDIPortConnectSource(inputPort, source, nil) == noErr {
                connectedSources.insert(source)
            }
        }
    }

    private func disconnectAllSources() {
        for source in connectedSources {
            MIDIPortDisconnectSource(inputPort, source)
        }
        connectedSources.removeAll()
    }

    private func handle(packetList: MIDIPacketList) {
        var packet = packetList.packet
        for _ in 0..<packetList.numPackets {
            withUnsafePointer(to: packet.data) { pointer in
                pointer.withMemoryRebound(to: UInt8.self, capacity: Int(packet.length)) { bytes in
                    parse(bytes: Array(UnsafeBufferPointer(start: bytes, count: Int(packet.length))))
                }
            }
            packet = MIDIPacketNext(&packet).pointee
        }
    }

    private func parse(bytes: [UInt8]) {
        var index = 0
        while index < bytes.count {
            let first = bytes[index]

            if first >= 0xf8 {
                index += 1
                continue
            }

            let status: UInt8
            if first >= 0x80 {
                status = first
                runningStatus = status < 0xf0 ? status : nil
                index += 1
            } else if let runningStatus {
                status = runningStatus
            } else {
                index += 1
                continue
            }

            let message = status & 0xf0
            let dataLength: Int
            switch message {
            case 0x80, 0x90, 0xa0, 0xb0, 0xe0:
                dataLength = 2
            case 0xc0, 0xd0:
                dataLength = 1
            default:
                index += 1
                continue
            }

            guard index + dataLength <= bytes.count else { return }
            let data1 = bytes[index]
            let data2 = dataLength > 1 ? bytes[index + 1] : 0
            index += dataLength

            DispatchQueue.main.async { [weak self] in
                self?.handleMessage(status: status, data1: data1, data2: data2)
            }
        }
    }

    private func handleMessage(status: UInt8, data1: UInt8, data2: UInt8) {
        let message = status & 0xf0
        let channel = Int(status & 0x0f) + 1
        if let receiveChannel, receiveChannel != channel {
            return
        }

        switch message {
        case 0x80:
            synth?.midiNoteOff(Int(data1))
        case 0x90:
            if data2 == 0 {
                synth?.midiNoteOff(Int(data1))
            } else {
                synth?.midiNoteOn(Int(data1), velocity: Int(data2))
            }
        case 0xb0:
            handleControlChange(number: data1, value: data2)
        case 0xe0:
            let raw = Int(data1) | (Int(data2) << 7)
            let normalized = max(-1.0, min(1.0, (Double(raw) - 8192.0) / 8192.0))
            synth?.setPitchWheel(normalized)
        default:
            break
        }
    }

    private func handleControlChange(number: UInt8, value: UInt8) {
        switch number {
        case 1:
            synth?.setModWheel(Double(value) / 127.0)
        case 64:
            synth?.setSustainPedal(value >= 64)
        case 65:
            synth?.setPortamentoFootSwitch(value >= 64)
        default:
            break
        }
    }

    private func uniqueID(for endpoint: MIDIEndpointRef) -> Int32 {
        var value: Int32 = 0
        MIDIObjectGetIntegerProperty(endpoint, kMIDIPropertyUniqueID, &value)
        if value == 0 {
            return Int32(endpoint)
        }
        return value
    }

    private func displayName(for endpoint: MIDIEndpointRef) -> String {
        var name: Unmanaged<CFString>?
        MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &name)
        if let displayName = name?.takeRetainedValue() as String?, !displayName.isEmpty {
            return displayName
        }

        var endpointName: Unmanaged<CFString>?
        MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &endpointName)
        if let fallback = endpointName?.takeRetainedValue() as String?, !fallback.isEmpty {
            return fallback
        }

        return "MIDI Input \(uniqueID(for: endpoint))"
    }
}
