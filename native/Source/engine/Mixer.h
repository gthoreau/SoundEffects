// Mixer — the output stage, SPEC §6/§2.1/§3. Ported from `src/audio/mixer.ts`.
//
// Packed buffers become float ONLY here, at the output edge. No DSP beyond
// summing and scaling. Routing is pure fidelity: each track's two output-channel
// toggles (L/R) are the same toggles that route the mix. The sum is NOT scaled
// by track count — two loud tracks clip, exactly as Quick Mix's mixdown clips.

#pragma once

#include "SoundDocument.h"

#include <array>
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

/**
 * How the monotonic transport position maps to each track's read position.
 *  - once:      each track reads at `pos`; past its own end it is absent (silent).
 *  - all:       each track loops over ITS OWN length independently — `pos % len`.
 *               Different lengths phase against each other (the core weirdness).
 *  - selection: the selected time range loops in phase; only the selected tracks
 *               sound; a track shorter than the read point is absent.
 */
enum class LoopMode { once, all, selection };

struct PlayContext
{
    LoopMode mode = LoopMode::once;
    size_t   selSt = 0, selEnd = 0;      // selection mode: the looped byte range
    int      firstSelTrack = 1, lastSelTrack = 1;   // selection mode: 1-based scope
};

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

} // namespace se
