// Mixer — the output stage, SPEC §6/§2.1/§3. Ported from `src/audio/mixer.ts`.
//
// Packed buffers become float ONLY here, at the output edge. No DSP beyond
// summing and scaling. Routing is pure fidelity: each track's two output-channel
// toggles (L/R) are the same toggles that route the mix. The sum is NOT scaled
// by track count — two loud tracks clip, exactly as Quick Mix's mixdown clips.

#pragma once

#include "SoundDocument.h"

#include <array>
#include <cstdint>
#include <set>

namespace se {

/** The 8-step volume popup: Off, 1..7. Linear ladder to unity. */
constexpr std::array<double, 8> VOLUME_STEPS { 0.0, 1.0/7, 2.0/7, 3.0/7, 4.0/7, 5.0/7, 6.0/7, 1.0 };
constexpr int VOLUME_OFF  = 0;
constexpr int VOLUME_FULL = 7;

double volumeScalar (int step) noexcept;

/**
 * Per-track monitoring, driven by the function keys: F1..Fn = solo track n,
 * option-Fn = mute track n. Solo is a filter, not a per-track flag.
 */
struct TrackMonitor
{
    std::set<int> muted;
    std::set<int> soloed;   // if non-empty, ONLY these tracks sound
};

bool isAudible (const TrackMonitor& monitor, int track) noexcept;

struct MixOptions
{
    int  volume = VOLUME_FULL;   // 0..7
    const TrackMonitor* monitor = nullptr;
    bool forceMono = false;      // a backend that mixes stereo down to mono
};

/** Which way a Window reads its tracks. Per Window, not per track: every track
    follows it, each over its own length and from its own phase. */
enum class PlayDirection : uint8_t { forward, backward, pingPong };

/** Tracks beyond this share the last track's phase (nobody has 64 lanes). */
constexpr int kPhaseTracks = 64;

/**
 * How the monotonic transport position maps to each track's read position.
 *  - once:      one span `[selSt, selEnd)` is played through, once, and the same span
 *               for every track; a track shorter than the read point is absent.
 *  - all:       each track loops over ITS OWN length independently.
 *               Different lengths phase against each other (the core weirdness).
 *  - selection: the selected time range loops in phase; only the selected tracks
 *               sound; a track shorter than the read point is absent.
 *
 * Direction: a track's span (`S` frames) is read through its own phase. With
 * u = (pos + phase[t]) in frames,
 *  - forward:   frame u            (looping: u mod S)
 *  - backward:  frame S-1-u        (looping: S-1-(u mod S))
 *  - pingPong:  u mod 2S folded back on itself, so it runs up, then down.
 * `phase` is what lets the direction change in mid-flight without a jump: the
 * transport re-anchors each track's phase so that the new mapping lands on the
 * frame the old one was reading, and heads off the way it was already going.
 */
enum class LoopMode { once, all, selection };

struct PlayContext
{
    LoopMode mode = LoopMode::once;
    size_t   selSt = 0, selEnd = 0;      // once / selection: the played byte range
    int      firstSelTrack = 1, lastSelTrack = 1;   // selection mode: 1-based scope
    PlayDirection dir = PlayDirection::forward;
    std::array<double, kPhaseTracks> phase {};     // bytes, one per track
};

/** Where one track reads, at transport position `pos`. */
struct TrackRead
{
    bool   inSpan = false;    // the track is part of what plays at all
    bool   present = false;   // ...and the read point is inside its data
    bool   up = true;         // reading toward higher frames
    size_t frame = 0;         // absolute frame index in the track (valid if inSpan)
    size_t lo = 0, span = 0;  // the span, in frames
    bool   wrap = false;      // the span loops
};

/** The span (frames) a track plays in; false when it does not play at all. */
bool readSpan (const PlayContext& ctx, int track, size_t lenFrames, size_t frameSize,
               size_t& lo, size_t& span, bool& wrap) noexcept;

/** `lenFrames` is the track's own length; the once span does not depend on it. */
TrackRead readFor (const PlayContext& ctx, int track, size_t lenFrames, size_t frameSize, double pos) noexcept;
TrackRead trackRead (const SoundDocument& doc, int track, double pos, const PlayContext& ctx) noexcept;

/** The phase (bytes) that makes `frame` of a span of `span` frames the one read at
    `pos`, heading `up` as far as the direction allows. `uFrames` returns the
    unwrapped position in frames, which the once stop point needs. */
double phaseToRead (PlayDirection dir, size_t span, size_t frameInSpan, bool up,
                    double pos, size_t frameSize, int64_t* uFrames = nullptr) noexcept;

struct StereoFrame { float left = 0.0f; float right = 0.0f; };

/**
 * Mix one frame at monotonic transport position `pos` into a stereo frame, each
 * track mapped through `ctx` to its own read position (independent per-track
 * looping). Absence (past a track's end) contributes nothing — the rest play on.
 * Per-track 8.8 volume (over-unity overdrives), L/R routing, no count scaling,
 * clamp at the edge.
 */
StereoFrame mixFrame (const SoundDocument& doc, size_t pos,
                      const PlayContext& ctx, const MixOptions& opts) noexcept;

/** The same, at a fractional position (the transport's own). */
StereoFrame mixFrameAt (const SoundDocument& doc, double pos,
                        const PlayContext& ctx, const MixOptions& opts) noexcept;

} // namespace se
