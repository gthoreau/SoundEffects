// Wav — one Track as a mono PCM .wav, and back, byte for byte.
//
// A project folder keeps each Track's audio in Assets/ as a plain WAV, so the
// stems open in any editor. The Track's own bytes (offset-binary 8-bit, otherwise
// big-endian) are only re-ordered, never converted, so a save/load is lossless at
// every depth the engine allows: a 12-bit Sound is a 16-bit file whose low 4 bits
// are zero, and its true depth lives in the .sefx. The WAV header can only say an
// integer rate, so it says round(rate); the exact one is in the .sefx too.
// JUCE-free.

#pragma once

#include "Track.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace se {

/** The WAV for `track` at `bps` (1..32). `out` is replaced. */
void encodeTrackWav (const Track& track, double rateHz, int bps, std::vector<uint8_t>& out);

/** Pull the audio bytes back out, in the Track's own layout. Refuses (false, `data`
    untouched) anything that is not 1-channel integer PCM whose container is exactly
    this depth's frame, or whose data chunk is not `expectedBytes` long. */
bool decodeTrackWav (const uint8_t* wav, size_t size, int bps, uint64_t expectedBytes,
                     std::vector<uint8_t>& data);

} // namespace se
