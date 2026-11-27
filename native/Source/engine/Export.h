// Export — a Sound as audio other programs read: File ▸ Export Sound...'s two .WAV
// formats (0.6.0). JUCE-free; Project.h writes the files.
//
// Both keep the Sound's own sample rate and bit depth.

#pragma once

#include "EngineState.h"

#include <cstdint>
#include <string>
#include <vector>

namespace se {

/** Two channels of equal length, in a Track's byte layout. */
struct Mixdown
{
    Track left, right;
};

/** What the Sound's mixer strip puts out, from the start of every track to the end of
    the longest: each track at its own level on its L / R / both, the sum clipped as
    the mix clips it, then the strip's volume and balance. Main Level, Main Mute and
    the Sound's own mute and solo play no part. Rounded to the nearest step of the
    Sound's depth and clipped there; never normalised. A track routed nowhere is
    silent, a track shorter than the longest is silent past its end. */
Mixdown renderMixdown (const WindowState& sound);

/** The mixdown as a stereo PCM .wav. `out` is replaced. */
void encodeMixdownWav (const Mixdown&, double rateHz, int bps, std::vector<uint8_t>& out);

/** One stem: a track as it is (before any level, routing or fader), as a mono .wav
    byte for byte, called "<Sound name> - track T.wav". */
struct Stem
{
    std::string fileName;
    std::vector<uint8_t> wav;
};

std::vector<Stem> splitStems (const WindowState& sound);

} // namespace se
