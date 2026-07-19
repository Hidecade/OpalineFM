## Opaline FM v1.0.9

This release improves DX21 compatibility, real-time audio performance, and platform reliability.

- Added single-voice import for common YM2151 and YM2612 voice formats.
- Refined DX21 operator level, detune, transpose, Level Scaling, and Rate Scaling behavior from hardware measurements.
- Reduced audio-thread locking, allocation, redundant parameter work, oscilloscope overhead, and inactive voice/FX processing.
- Moved WAV recording work away from the real-time audio path.
- Improved oscilloscope stability on desktop and iOS.
- Added DX21 voice export over MIDI.
- Improved iOS and AUv3 audio recovery and stability.
- Added cross-platform CI build and test coverage.

macOS packages are signed and notarized. Windows installers are currently unsigned.
