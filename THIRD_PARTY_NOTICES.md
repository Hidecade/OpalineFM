# Third-Party Notices

This file summarizes third-party components that may be used when building or distributing Opaline FM. It is not a substitute for the upstream license texts.

## JUCE

Opaline FM is built with JUCE. The checked-out JUCE source under `external/JUCE` includes its own license file at `external/JUCE/LICENSE.md`.

JUCE is available under multiple licensing options, including the AGPLv3/commercial terms described by JUCE. Binary distribution must follow the JUCE license option selected by the distributor.

## VST3 SDK

JUCE includes Steinberg VST3 SDK files used for VST3 builds. The relevant license files are under JUCE's VST3 SDK directories, including:

- `external/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt`
- `external/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/base/LICENSE.txt`
- `external/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/public.sdk/LICENSE.txt`
- `external/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/pluginterfaces/LICENSE.txt`

Include the applicable VST3 SDK notices when distributing VST3 binary builds.

## Apple Audio Unit SDK

JUCE includes Apple Audio Unit SDK files for Audio Unit builds. See:

- `external/JUCE/modules/juce_audio_plugin_client/AU/AudioUnitSDK/LICENSE.txt`

## ASIO

The build configuration enables JUCE ASIO support on platforms where it is available. JUCE includes ASIO-related source under:

- `external/JUCE/modules/juce_audio_devices/native/asio/LICENSE.txt`

Check the applicable ASIO licensing terms before distributing Windows binaries that include ASIO support.

## Other Bundled JUCE Dependencies

JUCE also includes notices for bundled dependencies such as zlib, libpng, SheenBidi, Oboe, and other platform support code. Review `external/JUCE/LICENSE.md` and the license files below `external/JUCE/modules` before distribution.
