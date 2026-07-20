# Opaline FM Factory Variant v1 Provenance

## Scope

`assets/factory_variant_v1.syx` and
`assets/factory_variant_v1.opalinelibrary.xml` are audition candidates derived
from the current `assets/factory.syx` library. They do not replace the bundled
factory library.

## Transformation

`Source/Tools/GenerateFactoryVariant.cpp` changes 57 of the 66 numeric voice
and effect parameters for every source voice (86.4%). It preserves algorithm,
transpose, LFO sensitivity and waveform settings, while applying bounded
changes to envelopes, levels, detune, scaling, velocity response, selected
ratios, LFO depths, pitch envelope, and effects. This keeps a recognizable
relationship with the source while producing a distinct parameter set.

The SysEx file contains the transformed compatible voice data. The XML file
contains the same Bank 1 voices plus transformed effect settings; Banks 2-8
are empty.

## Rights Status

This is a derivative parameter library, not a clean-room work. A high parameter
change percentage is an engineering measurement and does not establish a
copyright clearance threshold. The clean-room alternative remains
`factory_original_v1.*`.

This document records engineering provenance and is not legal advice.
