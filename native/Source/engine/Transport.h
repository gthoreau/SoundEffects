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

#pragma once

#include "Mixer.h"
#include "SoundDocument.h"

#include <atomic>

namespace se {

enum class TransportState { stopped, playing, paused };

/** "Start playing from Beginning / Insertion Point" — Preferences page 1. */
enum class PlayFrom { beginning = 1, insertionPoint = 2 };

struct PlayOptions
{
    bool   loop = false;            // loop button: loops the selection
    bool   useLoopRegion = false;   // option-Play: loop the Set Loop region instead
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
    void seek (const SoundDocument& doc, size_t bytes) noexcept;

    TransportState state() const noexcept { return currentState; }

    // --- UI snapshot: safe to read from the message thread while audio runs ---
    /** Monotonic transport byte position; the UI maps it per track (pos % len). */
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
     * range it plays it in — both frozen at play(), both empty while stopped.
     *
     * NOT the document's selection, and the difference matters: a running loop is
     * never re-aimed. Select somewhere else while it plays and the old region
     * keeps looping until you stop and play again. Anything drawing the loop — the
     * navigator's block, the playhead — has to read THIS, or it will show a region
     * the ear is not hearing.
     */
    size_t uiPlaySt()  const noexcept { return uiRegionSt.load (std::memory_order_relaxed); }
    size_t uiPlayEnd() const noexcept { return uiRegionEnd.load (std::memory_order_relaxed); }
    int uiFirstPlayTrack() const noexcept { return uiRegionFirst.load (std::memory_order_relaxed); }
    int uiLastPlayTrack()  const noexcept { return uiRegionLast.load (std::memory_order_relaxed); }

    int  masterVolume = VOLUME_FULL;   // 0..7
    TrackMonitor monitor;
    PlayFrom playFrom = PlayFrom::beginning;
    bool supportsStereo = true;        // false collapses L/R routing to a mono sum

    /** Fill left/right (planar, deviceRate) with up to `frames` frames from `doc`. */
    int render (const SoundDocument& doc, float* left, float* right, int frames) noexcept;

private:
    double step (const SoundDocument& doc) const noexcept;
    size_t framePos() const noexcept;   // frame-aligned pos using cachedFrameSize
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
    size_t playEnd = 0;         // once-mode stop point

    std::atomic<size_t>  uiPos { 0 };
    std::atomic<uint8_t> uiStateCode { static_cast<uint8_t> (TransportState::stopped) };
    std::atomic<uint8_t> uiLoopModeCode { static_cast<uint8_t> (LoopMode::once) };
    // Written only by play()/stop() — on the audio thread, like everything else
    // here, since transport commands arrive through the engine's queue.
    std::atomic<size_t>  uiRegionSt { 0 }, uiRegionEnd { 0 };
    std::atomic<int>     uiRegionFirst { 1 }, uiRegionLast { 1 };
};

} // namespace se
