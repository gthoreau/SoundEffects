// Track — one lane of a Window. SPEC §2.1. Ported from `src/document.ts`.
//
// Vocabulary: what the original 1994 app and the TS reference call a "channel"
// (ModAddNewChannel etc.) we call a **Track**. "Channel" is reserved for the
// per-track OUTPUT CHANNEL (L / R / L+R) and for the Mixer channel (a Window's
// strip). See the vocabulary note.
//
// The load-bearing weirdness: every track is an independent buffer with its OWN
// length. Nothing may assume tracks are equal length, pad them to match, or
// "repair" them. Tracks drifting out of alignment is a feature (§5).

#pragma once

#include <cstdint>
#include <vector>

namespace se {

/** 8.8 fixed volume: 0x0100 = 1.0. >1.0 overdrives, <=0 is silent. */
constexpr int VOLUME_UNITY = 0x0100;

struct Track
{
    std::vector<uint8_t> data;   // the audio sample: packed big-endian, offset-binary; size is BYTES
    bool outLeft  = true;        // output channel routing, left  (drives Quick Mix too)
    bool outRight = true;        // output channel routing, right
    int  volume   = VOLUME_UNITY; // 8.8 fixed
};

} // namespace se
