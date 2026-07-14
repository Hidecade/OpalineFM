# Opaline FM Mobile

Opaline FM Mobile is planned as a separate iPhone app that shares the Opaline FM synthesis engine while using a phone-first interface.

This folder is an initial scaffold. It is intentionally separate from the macOS standalone, VST3, and Audio Unit targets.

## Intended App Shape

- Play screen: voice selection, performance controls, keyboard, pitch wheel, and modulation wheel.
- Edit screen: algorithm, operators, envelopes, LFO, effects, and voice metadata.
- Library screen: factory/user banks, SysEx import/export, and saved patches.

## File Actions

- Top-row LOAD/SAVE manage the selected 32-voice `.syx` bank.
- Top-row EXPORT writes the full multi-bank `.opalinelibrary.xml` library.
- Voice-row LOAD/SAVE manage a single `.opalinevoice` patch.

## Shared Code

The iPhone app should reuse:

- `../../Source/Engine`
- `../../assets/factory.syx`

The mobile UI should avoid depending on the JUCE desktop/plugin UI. The current bridge in `Sources/Native` is a small Objective-C++ wrapper around `opaline::OpalineEngine` and related voice-library helpers.

## Project Generation

Requirements:

- Full Xcode installed from the App Store or Apple Developer Downloads.
- `xcode-select` pointing at the full Xcode app, not only Command Line Tools.
- XcodeGen.

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

## Current Status

This is an early playable-app scaffold. It now includes:

- SwiftUI Play/Edit/Library screens.
- AVAudioEngine output using `AVAudioSourceNode`.
- An Objective-C++ bridge to `opaline::OpalineEngine`.
- Bundled `factory.syx` loading into bank 1.

Next implementation steps:

- Wire all edit controls to the shared `OpalinePatch` model.
- Replace placeholder library voice names with the real bank contents.
- Improve keyboard touch tracking so repeated drag callbacks do not retrigger the same note.
- Add iPad layout and AUv3 extension only after the iPhone app flow is solid.
