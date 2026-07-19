#!/usr/bin/env swift

import CoreMIDI
import Foundation

struct Options {
    var destinationQuery: String?
    var velocity = 64
    var noteSeconds = 0.8
    var gapSeconds = 0.25
    var listOnly = false
}

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data("Error: \(message)\n".utf8))
    exit(1)
}

func parseOptions() -> Options {
    var options = Options()
    var index = 1
    let arguments = CommandLine.arguments

    while index < arguments.count {
        switch arguments[index] {
        case "--list":
            options.listOnly = true
        case "--destination":
            index += 1
            guard index < arguments.count else { fail("--destination requires a name") }
            options.destinationQuery = arguments[index]
        case "--velocity":
            index += 1
            guard index < arguments.count, let value = Int(arguments[index]), (1...127).contains(value) else {
                fail("--velocity must be between 1 and 127")
            }
            options.velocity = value
        case "--note-seconds":
            index += 1
            guard index < arguments.count, let value = Double(arguments[index]), value > 0 else {
                fail("--note-seconds must be greater than zero")
            }
            options.noteSeconds = value
        case "--gap-seconds":
            index += 1
            guard index < arguments.count, let value = Double(arguments[index]), value >= 0 else {
                fail("--gap-seconds must be zero or greater")
            }
            options.gapSeconds = value
        case "--help", "-h":
            print("""
            Usage: swift scripts/play-dx21-levelscale.swift [options]

              --list                  List CoreMIDI destinations
              --destination NAME      Select a destination by partial name
              --velocity VALUE        Note velocity (default: 64)
              --note-seconds VALUE    Note-on duration (default: 0.8)
              --gap-seconds VALUE     Silence after each note (default: 0.25)
            """)
            exit(0)
        default:
            fail("unknown option: \(arguments[index])")
        }
        index += 1
    }

    return options
}

func midiName(_ endpoint: MIDIEndpointRef) -> String {
    var unmanaged: Unmanaged<CFString>?
    guard MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &unmanaged) == noErr,
          let value = unmanaged?.takeRetainedValue() else {
        return "Unnamed MIDI destination"
    }
    return value as String
}

func destinations() -> [(MIDIEndpointRef, String)] {
    (0..<MIDIGetNumberOfDestinations()).map { index in
        let endpoint = MIDIGetDestination(index)
        return (endpoint, midiName(endpoint))
    }
}

func sendMessage(port: MIDIPortRef, destination: MIDIEndpointRef, bytes: [UInt8]) {
    var packetList = MIDIPacketList()
    withUnsafeMutablePointer(to: &packetList) { packetListPointer in
        let packet = MIDIPacketListInit(packetListPointer)
        bytes.withUnsafeBufferPointer { bytePointer in
            guard let baseAddress = bytePointer.baseAddress else { return }
            _ = MIDIPacketListAdd(packetListPointer,
                                  MemoryLayout<MIDIPacketList>.size,
                                  packet,
                                  0,
                                  bytes.count,
                                  baseAddress)
        }
        MIDISend(port, destination, packetListPointer)
    }
}

func waitForReturn(_ message: String) {
    print("\n\(message)")
    print("Audacityの録音を確認し、Returnキーで演奏を開始します。")
    _ = readLine()
}

func playNotes(_ notes: [Int],
               port: MIDIPortRef,
               destination: MIDIEndpointRef,
               options: Options) {
    waitForReturn("MIDI Note \(notes.map(String.init).joined(separator: ", "))を演奏します。")

    for note in notes {
        print("Note \(note)/127", terminator: "\r")
        fflush(stdout)
        sendMessage(port: port,
                    destination: destination,
                    bytes: [0x90, UInt8(note), UInt8(options.velocity)])
        Thread.sleep(forTimeInterval: options.noteSeconds)
        sendMessage(port: port,
                    destination: destination,
                    bytes: [0x80, UInt8(note), 64])
        Thread.sleep(forTimeInterval: options.gapSeconds)
    }
    print("Note \(notes.last ?? 0)/127")
}

let options = parseOptions()
let availableDestinations = destinations()

if options.listOnly {
    if availableDestinations.isEmpty {
        print("CoreMIDI destination was not found.")
    } else {
        for (index, destination) in availableDestinations.enumerated() {
            print("\(index): \(destination.1)")
        }
    }
    exit(0)
}

guard !availableDestinations.isEmpty else {
    fail("CoreMIDI destination was not found. Connect the USB MIDI interface and retry")
}

let selected: (MIDIEndpointRef, String)
if let query = options.destinationQuery {
    let matches = availableDestinations.filter { $0.1.localizedCaseInsensitiveContains(query) }
    guard matches.count == 1 else {
        let names = matches.map(\.1).joined(separator: ", ")
        fail(matches.isEmpty ? "destination was not found: \(query)" : "destination name is ambiguous: \(names)")
    }
    selected = matches[0]
} else if availableDestinations.count == 1 {
    selected = availableDestinations[0]
} else {
    print("複数のMIDI出力があります。--destination NAMEで選択してください。")
    for (index, destination) in availableDestinations.enumerated() {
        print("\(index): \(destination.1)")
    }
    exit(1)
}

var client = MIDIClientRef()
var outputPort = MIDIPortRef()
guard MIDIClientCreate("Opaline DX21 LevelScale Test" as CFString, nil, nil, &client) == noErr else {
    fail("MIDIClientCreate failed")
}
guard MIDIOutputPortCreate(client, "DX21 Test Output" as CFString, &outputPort) == noErr else {
    fail("MIDIOutputPortCreate failed")
}

print("MIDI destination: \(selected.1)")
print("Velocity: \(options.velocity), note: \(options.noteSeconds)s, gap: \(options.gapSeconds)s")

let testNotes = [0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 120, 127]
playNotes(testNotes, port: outputPort, destination: selected.0, options: options)

sendMessage(port: outputPort, destination: selected.0, bytes: [0xb0, 123, 0])
print("\nMIDIキーテストが完了しました。")

MIDIPortDispose(outputPort)
MIDIClientDispose(client)
