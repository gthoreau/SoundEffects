// Resample — ModResampleSoundRate / ModResampleSoundBits, ported from the TS
// reference `src/runtime/resample.ts` ([BINARY: Host-Resamplers.md]).
//
// Rate conversion is drop-sample / zero-order-hold (no interpolation); bit
// conversion is plain truncation / zero-extension (no rounding, no dither). Both
// mutate the document in place and reset its selection. Used to conform clipboard
// audio to a destination window's format on paste. No JUCE — testable.

#pragma once

#include "SoundDocument.h"

namespace se {

/** Convert every track to `newBps` (byte-width and/or bit-depth surgery). */
void resampleSoundBits (SoundDocument& doc, int newBps);

/** Resample every track to `newRateHz` (zero-order hold). Clamped to >= 1 Hz. */
void resampleSoundRate (SoundDocument& doc, double newRateHz);

} // namespace se
