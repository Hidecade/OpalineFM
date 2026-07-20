# Opaline FM Original Factory Voice Provenance

## Scope

`assets/factory_original_v1.syx` and
`assets/factory_original_v1.opalinelibrary.xml` contain 32 factory voices made
for Opaline FM. They are audition candidates and do not replace the currently
installed factory library.

## Creation Method

The voices were independently designed in
`Source/Tools/GenerateOriginalFactory.cpp`, starting from Opaline FM's default
`OpalinePatch` state. Each voice explicitly defines its own algorithm,
operator ratios, levels, envelopes, scaling, velocity response, LFO, pitch
envelope, and effects.

No third-party SysEx file, hardware factory bank, or third-party voice
parameter data is read by the generator or used as generator input. Voice
names are original and are limited to the compatible 10-character field.

The generator creates both deliverables from the same in-repository voice
definitions:

- `factory_original_v1.syx`: compatible 32-voice VMEM bulk SysEx
- `factory_original_v1.opalinelibrary.xml`: the same 32 voices with Opaline FM
  effect settings in Bank 1; Banks 2-8 are empty

## Reproducibility

Configure a JUCE-enabled build and run:

```sh
cmake --build <build-directory> --target opaline_generate_original_factory
<build-directory>/opaline_generate_original_factory assets
```

The generator validates the voice count, unique compatible names, per-voice
VMEM encode/decode round trips, and the complete bulk SysEx before writing
either file.

## Adoption Rule

The audition files must be reviewed by ear before release. If adopted, update
both the bundled SysEx and XML library together so desktop, plugin, iOS, and
AUv3 targets load identical voice data and retain the intended effect settings.

This document records engineering provenance and is not legal advice.
