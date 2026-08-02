# Opaline FM Mobile

Opaline FM Mobile is the iPhone and AUv3 version of Opaline FM. It shares the
C++ synthesis engine and voice formats with the desktop product while using a
phone-first SwiftUI interface and Apple audio/MIDI integration.

This project is intentionally separate from the macOS standalone, VST3, and
Audio Unit targets.

## Current App

- Play screen: voice selection, performance controls, keyboard, pitch wheel, and modulation wheel.
- Edit screen: algorithm, operators, envelopes, LFO, effects, and voice metadata.
- Library workflow: factory/user banks, SysEx import/export, and saved patches.
- AUv3 Instrument extension with factory voice selection, effects, Poly/Mono,
  portamento presets, parameter automation, and state restoration.
- Landscape keyboard with octave switching and Core MIDI input in the standalone app.

## File Actions

- Top-row LOAD/SAVE manage the selected 32-voice `.syx` bank.
- Top-row EXPORT writes the full multi-bank `.opalinelibrary.xml` library.
- Voice-row LOAD/SAVE manage a single `.opalinevoice` patch.

## Shared Code

The iPhone app reuses:

- `../../Source/Engine`
- `../../assets/factory.syx`

The mobile UI does not depend on the JUCE desktop/plugin UI. The bridge in
`Sources/Native` is an Objective-C++ wrapper around `opaline::OpalineEngine`
and the shared voice-library helpers.

## Project Generation

Requirements:

- Full Xcode installed from the App Store or Apple Developer Downloads.
- `xcode-select` pointing at the full Xcode app, not only Command Line Tools.
- XcodeGen.

The project includes a `project.yml` for XcodeGen:

```bash
cd iOS/OpalineFMMobile
xcodegen generate
open OpalineFMMobile.xcodeproj
```

If XcodeGen is not installed:

```bash
brew install xcodegen
```

If `xcodebuild` reports that the active developer directory is Command Line Tools, switch to Xcode:

```bash
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
```

Simulator build check:

```bash
xcodebuild \
  -project OpalineFMMobile.xcodeproj \
  -scheme OpalineFMMobile \
  -destination 'platform=iOS Simulator,name=iPhone 16' \
  -derivedDataPath ../../build/ios-mobile \
  build
```

## App Store Distribution

Use this checklist when preparing Opaline FM Mobile for TestFlight or App Store release.

1. Confirm the Apple Developer Program membership is active.
2. In Xcode, select the `OpalineFMMobile` target, open **Signing & Capabilities**, enable **Automatically manage signing**, and select the paid developer team.
3. Confirm the bundle identifier is stable, for example `com.hidekikonishi.opalinefm.mobile`. Do not change it after creating the App Store Connect record unless you intentionally want a separate app.
4. Increment the marketing version and build number in Xcode before each upload.
5. Build and test on a real iPhone, including audio output, MIDI input, file import/export, background interruption behavior, and landscape layout.
6. Create an app record in App Store Connect with the same bundle identifier.
7. In Xcode, choose **Product > Archive**, then open **Organizer** and upload the archive to App Store Connect.
8. Use TestFlight first. Add internal testers, then external testers if needed.
9. Prepare App Store metadata: app name, subtitle, description, keywords, support URL, privacy policy URL, screenshots, age rating, pricing, and availability.
10. Complete App Privacy and Export Compliance in App Store Connect. Opaline FM Mobile currently uses audio/MIDI/file features and should be checked carefully before submission.
11. Select the uploaded build, fill in review information, and submit the app for App Review.
12. After approval, choose manual release or automatic release from App Store Connect.

Suggested distribution policy:

- Pricing: free.
- Ads: none.
- In-app purchases: none unless a future paid feature is intentionally added.
- Support: use the GitHub repository Issues page or a dedicated support page/email.
- Privacy policy: state that the app is an audio instrument, does not show ads, and does not intentionally collect personal data. Recheck this if analytics, crash reporting, cloud sync, or account features are added.

Official Apple references:

- [Add a new app](https://developer.apple.com/help/app-store-connect/create-an-app-record/add-a-new-app)
- [Upload builds](https://developer.apple.com/help/app-store-connect/manage-builds/upload-builds/)
- [TestFlight overview](https://developer.apple.com/help/app-store-connect/test-a-beta-version/overview-of-testing-with-testflight/)
- [Submit an app](https://developer.apple.com/help/app-store-connect/manage-submissions-to-app-review/submit-an-app)
- [Manage app privacy](https://developer.apple.com/help/app-store-connect/manage-app-information/manage-app-privacy/)
- [Export compliance](https://developer.apple.com/help/app-store-connect/manage-app-information/overview-of-export-compliance/)

## Implementation Status

The shipping implementation includes:

- SwiftUI Play/Edit screens with the shared patch parameters wired to the C++ engine.
- AVAudioEngine output using `AVAudioSourceNode`.
- Objective-C++ engine and voice-library bridge.
- Bundled factory SysEx and Opaline library data with the real bank and voice names.
- Bank and single-voice file operations.
- AUv3 Instrument extension embedded in the app.

The current layout is optimized for landscape iPhone. A dedicated iPad layout
and real-device/host compatibility checks remain ongoing release work.
