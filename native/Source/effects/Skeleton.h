// The canonical `effect()` skeleton — SPEC §7.8, [SOURCE: Gain.c].
// Ported from the TS reference `src/runtime/skeleton.ts`.
//
// "Every recovered module in §8 is a variation on this skeleton — the differences
// are the inner kernel and whether the pass runs forward/backward, extends the
// buffer, or appends tracks." So this is the base every port sits on, and the
// ports should read like the C.
//
// The steps, in Gain.c's order, because the ORDER is load-bearing:
//   1. own resource file          5. clamp to THIS track's size, frame-align
//   2. read & free settings       6. the inner kernel  ← the only bit that varies
//   3. progress text              7. saturating clamp
//   4. empty selection ⇒ whole    8. poll progress (usually canCancel = false)

#pragma once

#include "Host.h"
#include "Results.h"
#include "Selection.h"
#include "engine/SoundDocument.h"

#include <functional>
#include <vector>

namespace se {

/** One track's clamped, frame-aligned range — Gain.c step 5. */
struct TrackPass
{
    /** 0-based, as Gain.c's `channel` loop variable. */
    int      index = 0;
    uint8_t* data = nullptr;
    size_t   size = 0;
    size_t   start = 0;
    size_t   end = 0;
};

/**
 * The per-track loop: `for (channel = firstSelCh-1; channel < lastSelCh; channel++)`
 * with each track clamped to its OWN size and frame-aligned.
 *
 * Skips a track whose range is empty (Gain.c's `if (start != end)`), which is
 * exactly how a track shorter than the selection gets passed over rather than
 * padded — the behaviour that lets tracks drift out of alignment (§5).
 *
 * NB `runSkeleton` does NOT call this: it recomputes each pass immediately before
 * running the body, matching the reference's generator, because a body may grow
 * its own track's buffer. This exists for callers that genuinely want the whole
 * set measured up front.
 */
std::vector<TrackPass> trackPasses (SoundDocument& doc);

/**
 * Floor division — NOT C's truncate-toward-zero.
 *
 * Smooth and the FIR filters average via a LOGICAL SHIFT on an offset-binary
 * (8-bit) or sign-flipped excess (9–32-bit) representation, and both of those
 * floor. So (-1 + 0)/2 is -1 here where C's signed `/2` gives 0. An easy detail
 * to lose, and audible on quiet material.
 */
inline int64_t floorDiv (int64_t n, int64_t d) noexcept
{
    const int64_t q = n / d;
    return (n % d != 0 && ((n < 0) != (d < 0))) ? q - 1 : q;
}

/**
 * The progress dance, fiddly enough to be worth encapsulating: the module polls
 * `timeToCallProgress` every sample and only calls showProgress when the host has
 * raised it. `tick()` returns true when the module should break out — LEAVING THE
 * HALF-PROCESSED AUDIO IN PLACE (§1.2).
 */
class Progress
{
public:
    Progress (Host& h, size_t total, bool canCancel) noexcept
        : host (h), totalFrames (total), cancellable (canCancel) {}

    /** Call once per sample, exactly where Gain.c calls it: after the write. */
    bool tick() noexcept
    {
        ++processed;
        if (! host.timeToCallProgress) return false;
        return host.showProgress (processed, totalFrames, cancellable) != kModNoError;
    }

    size_t count() const noexcept { return processed; }

private:
    Host&  host;
    size_t totalFrames = 0;
    bool   cancellable = false;
    size_t processed = 0;
};

struct SkeletonOptions
{
    /** The STR ' 128 progress text, e.g. "Amplifying by ". */
    std::string text;
    /**
     * Almost always false. Amplify, Echo, Delay, Noise, Smooth, Robotize and
     * Downsample Bits all hard-code canCancel = false — a long Echo genuinely
     * cannot be interrupted, and that is not an oversight to fix.
     */
    bool canCancel = false;
};

/**
 * Run the skeleton, delegating each track to `body`.
 *
 * Result follows Gain.c: start at kModCancel ("the module didn't do anything") and
 * promote to kModNoError once a track actually had samples in range. A body may
 * return a non-zero result to abort the whole run.
 */
ModResult runSkeleton (SoundDocument& doc, Host& host, const SkeletonOptions& opts,
                       const std::function<ModResult (const TrackPass&, Progress&)>& body);

/**
 * The commonest shape of all: a pointwise kernel, s' = f(s), clamped.
 *
 * This is Gain.c with step 6 swapped out. Amplify, Invert and Dither are one line
 * each on top of it.
 */
ModResult perSampleEffect (SoundDocument& doc, Host& host, const SkeletonOptions& opts,
                           const std::function<int64_t (int32_t sample, const TrackPass&, size_t offset)>& kernel);

/**
 * Resolve the doExtend policy for this run. [BINARY: Echo-Delay.md step 5]
 *
 * The pref's own value is used directly unless it is "Ask Each Time", which fires
 * a live AskDoExtend(). Echo and Delay both do exactly this.
 */
inline bool resolveDoExtend (Host& host) { return host.askDoExtend(); }

/**
 * Grow a track to `wanted` bytes and silence the new tail. [BINARY: Echo-Delay.md]
 *
 * Freshly grown memory is uninitialised garbage in the original, so the tail must
 * be silenced BEFORE anything is mixed into it. The original also re-queries the
 * achieved size, because a partial grow under memory pressure silently shrinks the
 * mix range with no error surfaced; we have no allocator that half-succeeds, so
 * that re-query is a no-op here — but the silencing is not.
 *
 * @returns the size actually achieved.
 */
size_t growAndSilence (SoundDocument& doc, int trackIndex, size_t wanted);

} // namespace se
