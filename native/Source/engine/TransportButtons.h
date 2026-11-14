// TransportButtons — what the transport buttons of one Window show, and what each
// press asks the engine to do (0.5.0 step 5, the spec's "Transport buttons" table).
//
// A pure model, no JUCE and no Engine: every press changes the lights and returns
// the cues the Window turns into engine and Recorder calls. The engine stays the
// truth about where a playhead is; this is the truth about which button is lit.
//
//   Record     off -> armed (blinks) -> off. Armed, Play starts the take. Arming
//              while Play is on starts the take at once (Record on). Disarming a
//              take ends it and playback carries on. ⇧R: direct recording.
//              While Record is armed or on, Direction, FF and Loop are disabled
//              (greyed, their values kept for when Record goes off again).
//   Stop       latches on; Play, Pause, Record and FF go off; playheads reset.
//   Play       latches on; Pause and Stop go off; an armed Record goes on.
//   Pause      latches on; Play goes off; Record on goes back to armed.
//   FF         momentary: on while held. From Play it only speeds up; from Pause
//              or Stop (or the initial state) it also plays, and letting go puts
//              back exactly the state it was pressed in.
//   Loop       toggles; on in the initial state.
//
// Initial state (a new or newly opened Sound): everything off but Loop.

#pragma once

#include <array>
#include <cstdint>

namespace se {

enum class RecordLight : uint8_t { off, armed, on };

/** What a press asks of the engine, in order. */
enum class TransportCue : uint8_t
{
    play,        // roll from stopped, with the Play button's options
    resume,      // paused -> playing
    pause,       // playing -> paused
    stop,        // stop playback and any take; reset the playheads
    takeStart,   // begin a take; the transport rolls with it (a rolling one is joined, not restarted)
    takePause,   // the take and its transport pause
    takeResume,  // ...and carry on
    takeEnd,     // the take ends; the transport carries on as it is, without a glitch
    fastOn,      // FF engaged (2×)
    fastOff      // FF let go (normal speed)
};

struct TransportCues
{
    std::array<TransportCue, 4> list {};
    int count = 0;

    void push (TransportCue c) noexcept { if (count < static_cast<int> (list.size())) list[static_cast<size_t> (count++)] = c; }
    const TransportCue* begin() const noexcept { return list.data(); }
    const TransportCue* end() const noexcept   { return list.data() + count; }
    bool empty() const noexcept { return count == 0; }
};

/** The engine transport's own state, as the model follows it (see engineChanged). */
enum class RollState : uint8_t { stopped, playing, paused };

class TransportButtons
{
public:
    /** Everything off, Loop on: a new or newly opened Sound. */
    static TransportButtons initial() noexcept { return {}; }

    // --- presses -----------------------------------------------------------------
    TransportCues pressRecord() noexcept;     // R / click: arm, disarm
    TransportCues directRecord() noexcept;    // ⇧R
    TransportCues pressStop() noexcept;       // Esc / click
    TransportCues pressPlay() noexcept;       // click
    TransportCues pressPause() noexcept;      // click
    TransportCues space() noexcept;           // Space: Pause when Play is on, else Play
    TransportCues ffDown() noexcept;          // mouse down on FF, or F pressed
    TransportCues ffUp() noexcept;            // ...released
    void toggleLoop() noexcept;               // L / click

    // --- what the engine did on its own --------------------------------------------
    /** The transport or the take came to an end by itself (play-once reached the
        end, a punch-out reached the selection's end): as a Stop, with nothing to cue. */
    void ended() noexcept;
    /** A take the model cued could not start (no source, too many tracks):
        Record goes off; Play stays on only if the transport is rolling anyway. */
    void takeRefused (bool transportRolling) noexcept;
    /** The engine's transport changed state under us (the Keyboard played it, a
        loop-off pass ran out). Edge-triggered on purpose: a cue takes a block to
        reach the engine, so a stale level must not undo a press. */
    void engineChanged (RollState was, RollState now) noexcept;

    // --- the lights ----------------------------------------------------------------
    RecordLight record() const noexcept { return rec; }
    bool stopOn() const noexcept  { return stopLit; }
    bool playOn() const noexcept  { return playLit; }
    bool pauseOn() const noexcept { return pauseLit; }
    bool ffOn() const noexcept    { return ffLit; }
    bool loopOn() const noexcept  { return loopLit; }
    /** A take exists (recording or held by Pause): Play resumes it rather than starting another. */
    bool takeOpen() const noexcept { return take; }
    bool ffHeld() const noexcept  { return held != Held::none; }

    bool directionEnabled() const noexcept { return rec == RecordLight::off; }
    bool ffEnabled() const noexcept        { return rec == RecordLight::off; }
    bool loopEnabled() const noexcept      { return rec == RecordLight::off; }

    bool operator== (const TransportButtons& o) const noexcept
    {
        return rec == o.rec && stopLit == o.stopLit && playLit == o.playLit && pauseLit == o.pauseLit
            && ffLit == o.ffLit && loopLit == o.loopLit && take == o.take && held == o.held;
    }
    bool operator!= (const TransportButtons& o) const noexcept { return ! (*this == o); }

private:
    enum class Held : uint8_t { none, fromPlay, fromPause, fromStop, fromIdle };

    /** Another press while FF is held: FF lets go where it is (no restore). */
    void dropHold (TransportCues& c) noexcept;
    TransportCues startTakeCue() noexcept;    // takeStart, or takeResume for a held take

    RecordLight rec = RecordLight::off;
    bool stopLit = false, playLit = false, pauseLit = false, ffLit = false, loopLit = true;
    bool take = false;
    Held held = Held::none;
};

} // namespace se
