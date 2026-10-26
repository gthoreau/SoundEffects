// Transport — the tape-recorder buttons, SPEC §3/§6. Ported from
// `src/audio/transport.ts`. One Transport per Window.
//
// Playback STATE only — it owns a position and a state machine, NOT the document.
// The document is passed in per call, so the engine can swap a Window's document
// underneath a running transport (RCU editing) without the transport dangling.
//
// The declared rate is obeyed even when it lies: the playhead advances at
// (doc.rateHz * rateScale * advanceScale) / deviceRate bytes per output frame.
// Loop semantics (the "exquisite mess"): with no selection the whole sound loops
// (tracks drift out of phase); with a selection, only the selected span loops.
//
// Direction (0.4.0): forward, backward or ping-pong, a property of the Window. The
// transport's `pos` is still one monotonic clock; each track reads through its own
// phase (PlayContext::phase) over its own span, so a direction change, or a loop
// range that is moved while it plays, only re-anchors those phases — the playhead
// of every track carries on from where it is — and a ~2 ms crossfade from the old
// mapping hides whatever step is left.

#pragma once

#include "Mixer.h"
#include "SoundDocument.h"

#include <array>
#include <atomic>

namespace se {

enum class TransportState { stopped, playing, paused };

/** "Start playing from Beginning / Insertion Point" — Preferences page 1. */
enum class PlayFrom { beginning = 1, insertionPoint = 2 };

struct PlayOptions
{
    bool   loop = false;            // loop button: loops the selection
    bool   useLoopRegion = false;   // loop the Set Loop region, when one is set, not the selection
    double rateScale = 1.0;         // Keyboard's chromatic auditioning (§8 #18)
    int    soloTrack = -1;          // F1..Fn soloing (0-based); -1 = none
};

class Transport
{
public:
    explicit Transport (double deviceRate);

    void setDeviceRate (double rate) noexcept { deviceRate = rate; }

    void play (const SoundDocument& doc, const PlayOptions& opts = {});
    void stop() noexcept;
    void pause() noexcept;                       // space toggles play/pause
    void advance (const SoundDocument& doc);     // play at 2× (see .cpp)
    void normalSpeed() noexcept { advanceScale = 1.0; }

    /**
     * Change the direction. Stopped: it is kept for the next play(). Rolling or
     * paused: every track keeps the frame it is on and carries on from there in the
     * new direction (see anchor()), under a micro-crossfade.
     */
    void setDirection (PlayDirection d, const SoundDocument& doc) noexcept;
    PlayDirection direction() const noexcept { return dir; }
    PlayDirection uiDirection() const noexcept
    {
        return static_cast<PlayDirection> (uiDirCode.load (std::memory_order_relaxed));
    }

    /**
     * Change the rate multiplier of a transport that is ALREADY ROLLING.
     *
     * This is how the Keyboard pitches a Window that is already playing without
     * interrupting it. The playhead keeps its position and only the step size
     * changes from the next frame on — so there is no gap, no phase reset, and no
     * re-aiming of the loop region, all three of which a fresh play() would cause
     * (it re-snapshots the region from the document's CURRENT selection).
     *
     * A no-op on a stopped transport in every sense that matters: it is stored,
     * and the next play() overwrites playOpts wholesale.
     */
    void setRateScale (double scale) noexcept { playOpts.rateScale = scale; }
    void seek (const SoundDocument& doc, size_t bytes) noexcept;

    TransportState state() const noexcept { return currentState; }

    // --- UI snapshot: safe to read from the message thread while audio runs ---
    /** Monotonic transport byte position (the shared clock; see uiTrackPosition()). */
    size_t uiPlayhead() const noexcept { return uiPos.load (std::memory_order_relaxed); }
    TransportState uiState() const noexcept
    {
        return static_cast<TransportState> (uiStateCode.load (std::memory_order_relaxed));
    }
    LoopMode uiLoopMode() const noexcept
    {
        return static_cast<LoopMode> (uiLoopModeCode.load (std::memory_order_relaxed));
    }

    /**
     * The byte range this transport is ACTUALLY playing, and the 1-based track
     * range it plays it in — both empty while stopped.
     *
     * A running loop follows the document: move the selection (or Set Loop) while
     * it plays and the loop is re-aimed at the next block (see render()), so this
     * changes with it. Anything drawing the loop — the navigator's block — reads
     * THIS rather than the selection, so it shows what the ear is hearing.
     */
    size_t uiPlaySt()  const noexcept { return uiRegionSt.load (std::memory_order_relaxed); }
    size_t uiPlayEnd() const noexcept { return uiRegionEnd.load (std::memory_order_relaxed); }
    int uiFirstPlayTrack() const noexcept { return uiRegionFirst.load (std::memory_order_relaxed); }
    int uiLastPlayTrack()  const noexcept { return uiRegionLast.load (std::memory_order_relaxed); }

    /**
     * Where track `t` (0-based) is reading, in bytes, as of the last block; false
     * while it is silent (stopped, out of the loop's tracks, or past its own end).
     * Safe from the message thread. The playhead of a track is drawn from this: with
     * a direction, the track's position is no longer a function of uiPlayhead().
     */
    bool uiTrackPosition (int t, size_t& bytes) const noexcept
    {
        if (t < 0 || t >= kPhaseTracks) return false;
        const size_t v = uiTrackPos[static_cast<size_t> (t)].load (std::memory_order_relaxed);
        if (v == kNoPosition) return false;
        bytes = v;
        return true;
    }

    int  masterVolume = VOLUME_FULL;   // 0..7
    TrackMonitor monitor;
    PlayFrom playFrom = PlayFrom::beginning;
    bool supportsStereo = true;        // false collapses L/R routing to a mono sum
    /** Tracks that stay silent while they are being recorded into (bit t = track t,
        0-based). Audio thread, set by the record commands. */
    uint64_t excludeMask = 0;

    /** Fill left/right (planar, deviceRate) with up to `frames` frames from `doc`. */
    int render (const SoundDocument& doc, float* left, float* right, int frames) noexcept;

private:
    double step (const SoundDocument& doc) const noexcept;
    size_t framePos() const noexcept;   // frame-aligned pos using cachedFrameSize

    /** What a loop would play right now: the Set Loop region, else the selection,
        else every track whole — the same answer play() and the live re-aim give. */
    struct Aim { LoopMode mode = LoopMode::all; size_t st = 0, end = 0; int first = 1, last = 1; };
    static Aim loopAim (const SoundDocument& doc, const PlayOptions& opts) noexcept;

    /** Make `next` (mode, span, direction set; phases not) the live mapping: each
        track's phase is re-derived so it keeps reading the frame it is on, heading
        the way it was heading; a track that has no such frame in the new span
        restarts at the span's start. The old mapping fades out underneath. */
    void anchor (const SoundDocument& doc, PlayContext next) noexcept;
    void publishRegion (size_t st, size_t end, int first, int last) noexcept;
    void publishTrackPositions (const SoundDocument& doc) noexcept;
    void startCrossfade (const PlayContext& from) noexcept;

    static constexpr size_t kNoPosition = static_cast<size_t> (-1);
    void setState (TransportState s) noexcept
    {
        currentState = s;
        uiStateCode.store (static_cast<uint8_t> (s), std::memory_order_relaxed);
    }

    double deviceRate;

    TransportState currentState = TransportState::stopped;
    double pos = 0.0;           // MONOTONIC byte position; fractional device-rate step
    double advanceScale = 1.0;  // 2.0 while Advance is engaged
    int    cachedFrameSize = 1; // last doc's frameSize, for framePos()
    PlayOptions playOpts;
    PlayContext ctx;            // how pos maps to each track's read position
    double stopPos = 0.0;       // once-mode stop point (a pos value)
    PlayDirection dir = PlayDirection::forward;

    // The mapping we were on, faded out over the first `xfadeTotal` frames after a
    // re-anchor. Plain copy: the audio thread never allocates.
    PlayContext prev;
    int xfadeLeft = 0, xfadeTotal = 1;

    std::atomic<size_t>  uiPos { 0 };
    std::atomic<uint8_t> uiStateCode { static_cast<uint8_t> (TransportState::stopped) };
    std::atomic<uint8_t> uiDirCode { static_cast<uint8_t> (PlayDirection::forward) };
    std::array<std::atomic<size_t>, kPhaseTracks> uiTrackPos;
    std::atomic<uint8_t> uiLoopModeCode { static_cast<uint8_t> (LoopMode::once) };
    // Written only by play()/stop() — on the audio thread, like everything else
    // here, since transport commands arrive through the engine's queue.
    std::atomic<size_t>  uiRegionSt { 0 }, uiRegionEnd { 0 };
    std::atomic<int>     uiRegionFirst { 1 }, uiRegionLast { 1 };
};

} // namespace se
