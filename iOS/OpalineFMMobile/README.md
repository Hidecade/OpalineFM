# Opaline FM Mobile

Opaline FM Mobile is planned as a separate iPhone app that shares the Opaline FM synthesis engine while using a phone-first interface.

This folder is an initial scaffold. It is intentionally separate from the macOS standalone, VST3, and Audio Unit targets.

## Intended App Shape

- Play screen: voice selection, performance controls, keyboard, pitch wheel, and modulation wheel.
- Edit screen: algorithm, operators, envelopes, LFO, effects, and voice metadata.
- Library screen: factory/user banks, SysEx import/export, and saved patches.

## Shared Code

The iPhone app should reuse:

- `../../Source/Engine`
- `../../assets/factory.syx`

The mobile UI should avoid depending on the JUCE desktop/plugin UI. The current bridge in `Sources/Native` is a small Objective-C++ wrapper around `opaline::OpalineEngine` and related voice-library helpers.

## Project Generation

The scaffold includes a `project.yml` for XcodeGen:

```bash
cd iOS/OpalineFMMobile
xcodegen generate
open OpalineFMMobile.xcodeproj
```

If XcodeGen is not installed:

```bash
brew install xcodegen
```

## Current Status

This is a folder and source scaffold, not a finished playable iPhone build yet.

Next implementation steps:

- Add the real-time iOS audio render path using AVAudioEngine or Audio Units.
- Feed the render callback from `OpalineMobileEngineBridge::renderLeft:right:frames:`.
- Load `factory.syx` from the app bundle.
- Wire all edit controls to the shared `OpalinePatch` model.
- Add iPad layout and AUv3 extension only after the iPhone app flow is solid.
