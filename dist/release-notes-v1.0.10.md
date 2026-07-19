## Opaline FM v1.0.10

Opaline FM is a four-operator FM synthesizer inspired by classic 1980s digital FM instruments. This release provides standalone applications and plug-ins for Windows and macOS, together with the iPhone app and AUv3 extension.

### Highlights

- Four-operator FM synthesis with eight algorithms, operator feedback, Pitch EG, amplitude EG, LFO, keyboard scaling, velocity, and operator AM.
- SINGLE, DUAL, and SPLIT performance modes with POLY/MONO operation, pitch bend, modulation, sustain, and Full/Fingered portamento.
- Built-in reverb, delay, chorus, wet-mix, and tone controls.
- Voice editing, copy/paste, initialization, storage, library management, and WAV recording in the standalone application.
- Compatible 32-voice SysEx bank import/export and complete multi-bank Opaline library import/export.
- Single-voice import from Opaline, YM2151, and YM2612-related OPM, TFI, VGI, and supported DMP files.

### DX21 Voice Transfer

- Added **VOICE EXPORT** to send the current compatible voice to a connected DX21 over MIDI.
- **VOICE EXPORT** is available in the desktop standalone interface, including the plugin-based standalone wrapper.
- It remains hidden inside VST3 and AU hosts.
- The development-only **MIDI KEY TEST** control remains hidden from the release interface.

### Platforms and Formats

- Windows x64: standalone application and VST3 instrument.
- macOS: standalone application, VST3 instrument, and Audio Unit instrument.
- iPhone: standalone app with AUv3 Audio Unit extension for compatible hosts such as GarageBand.

The iPhone AUv3 interface focuses on factory voice selection, effects, MONO mode, and portamento. Detailed sound editing and library management are available in the main Opaline FM app.

### Compatibility Notes

- v1.0.10 retains the v1.0.9 synthesis behavior.
- Imported YM2151/YM2612 parameters are converted to the nearest available Opaline FM values where the source formats and synthesis models differ.
- Opaline FM uses its own FM rendering engine and is not a chip emulator.

### Installation

Download the installer matching your operating system and plug-in format from the assets below. The macOS packages are signed and notarized. Windows installers are currently unsigned and may display a publisher warning.
