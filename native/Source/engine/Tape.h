// Tape — the Magnetophon (0.5.0 step 11): a basic reel-to-reel tape recorder that
// records the Main output, one Sound, an external input or the DAW input into one
// growing tape, with takes as start markers. No JUCE.
//
//   - `Tape` is the pure model: length, position, takes, what is playing or
//     recording, what has not been sent to the canvas yet. No audio, no threads.
//   - `TapeDeck` drives the Engine (its tape capture and playback) from the message
//     thread, as the Recorder does for a Sound, and keeps the model in step.
//
// Behaviour (spec "NEW - Magnetophon", decisions of 2026-11-15):
//   - Record starts a take where the tape is: a start marker at the position. At
//     the end of the tape that appends; in the middle it RECORDS OVER, tape-like:
//     the audio is replaced only as far as the new recording runs, the take markers
//     it runs over are erased, and the audio and takes after it stay where they are.
//     A take that ends inside older audio leaves that older tail as a take of its
//     own (a marker at the point recording stopped), so a take is exactly what was
//     recorded in it.
//   - Record while recording starts the next take there and then, with no gap.
//   - Stop ends the take (or the playback) where the tape is.
//   - Play plays from the position across takes to the end of the tape, then stops
//     there. At the end of the tape, Play replays the last take from its start.
//   - Next / Previous jump to take starts. Previous inside a take, past a short
//     grace, goes back to that take's start first (as a CD player does). During
//     playback the jump plays on from there; during recording both are refused.
//   - Delete a take (0.6.0): its audio goes, what follows closes up and the takes
//     are numbered afresh; no undo. A position inside it moves to where the next take
//     now starts, and a playing tape plays on from there (after the last take, it
//     stops). Refused while recording.
//   - The tape is ephemeral (it lives for the session). Takes not sent to the canvas
//     yet count as unsent audio, for the close-canvas warning.

#pragma once

#include "Engine.h"
#include "RecordConvert.h"
#include "SoundDocument.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace se {

class Tape
{
public:
    enum class Mode { stopped, recording, playing };

    struct Take { size_t start = 0; bool sent = false; };

    // --- state ---------------------------------------------------------------
    Mode   mode() const noexcept { return md; }
    size_t length() const noexcept { return len; }
    size_t position() const noexcept { return pos; }
    const std::vector<Take>& takes() const noexcept { return list; }
    int    numTakes() const noexcept { return static_cast<int> (list.size()); }
    /** Frames of take `i`: from its start to the next one's, or the tape's end. */
    size_t takeStart (int i) const noexcept;
    size_t takeEnd (int i) const noexcept;
    /** The take that holds `frame` (the last one at or before it), -1 on a blank tape. */
    int    takeAt (size_t frame) const noexcept;
    /** The take being recorded, or -1. */
    int    recordingTake() const noexcept { return md == Mode::recording ? takeAt (recStart) : -1; }
    bool   empty() const noexcept { return len == 0; }
    /** A take holds audio that has not been sent to the canvas. */
    bool   hasUnsent() const noexcept;
    /** The device rate the tape was recorded at (0 while blank). */
    double rate() const noexcept { return tapeRate; }

    // --- operations (each returns false when it does nothing) -----------------
    /** Start a take at the position, recording at `deviceRate` (a blank tape takes
        the rate; a tape at another rate refuses: its audio would change pitch).
        While recording: the next take starts where the head is. */
    bool record (double deviceRate);
    /** The recording head wrote `n` frames at the position: it moves on, the tape
        grows past its end, and markers it ran over go. */
    void recorded (size_t n);
    /** End the take or the playback where the tape is. */
    bool stop();
    /** Play from the position (at the end: from the last take's start). */
    bool play();
    /** Playback reached `frame`; at the end of the tape it stops by itself. */
    void played (size_t frame);
    bool nextTake();
    /** `graceFrames`: inside a take further than this from its start, go to that
        start rather than to the take before. */
    bool previousTake (size_t graceFrames);
    /** Move the position (not while recording); clamped to the tape. */
    bool seek (size_t frame);
    /** Take `i` and its frames come out of the tape (not while recording). */
    bool removeTake (int i);

    void markSent (int take) noexcept { if (take >= 0 && take < numTakes()) list[(size_t) take].sent = true; }
    void markAllSent() noexcept { for (auto& t : list) t.sent = true; }
    /** Blank tape, at 0, stopped. */
    void clear();

private:
    Mode md = Mode::stopped;
    size_t len = 0, pos = 0;
    size_t recStart = 0;
    std::vector<Take> list;   // sorted by start, all < len once anything is recorded
    double tapeRate = 0.0;
    bool tailSent = false;      // recording: the "sent" flag of the audio after the head
    bool replaced = false;      // recording: a marker stood where the take began...
    bool replacedSent = false;  // ...and this was its flag
};

/** The Engine-facing deck. Message thread only. */
class TapeDeck
{
public:
    explicit TapeDeck (Engine& e) : engine (e) {}
    ~TapeDeck();

    const Tape& tape() const noexcept { return model; }
    Tape& tape() noexcept { return model; }

    /** Where takes come from. Changing it while recording is refused. */
    bool setSource (const TapeSource& s);
    const TapeSource& source() const noexcept { return src; }

    bool record();
    bool stop();
    bool play();
    bool nextTake();
    bool previousTake();
    /** Move the tape (not while recording); a playing tape plays on from there. */
    bool seek (size_t frame);

    /** Wind the tape by hand, heard (the panel's strip): begin, move, let go. A
        playing tape stops while it is held and plays on from where it is let go. */
    bool scrubBegin();
    void scrubTo (size_t frame);
    void scrubEnd();
    bool scrubbing() const noexcept { return scrubOn; }

    /** A stop that is still collecting the last of a take from the audio thread. */
    bool finishing() const noexcept { return stopping; }
    /** Blank the tape (stops it first; the memory goes once the audio thread let go). */
    void clear();
    /** Delete take `i`, audio and all. A playing tape fades out first and the audio
        closes up once the audio thread has let go (at once if it is not reading, else
        in a later service()); it then plays on. Until then erasing() holds and the
        deck refuses to move. */
    bool deleteTake (int i);
    bool erasing() const noexcept { return erasingTake >= 0; }
    /** Goes up each time audio is taken off the tape (a delete, a clear). */
    uint32_t cuts() const noexcept { return numCuts; }

    /** Message thread, regularly (30 Hz): drain the capture onto the tape, follow
        playback, finish what is finishing. */
    void service();

    /** Copy `n` frames from `from` out of the tape (for Send to canvas). */
    size_t copy (size_t from, float* l, float* r, size_t n) const noexcept
    {
        return engine.tapeStore().read (from, l, r, n);
    }

    /** Frames of audio the tape received since the last call, as [from, to). Lets
        the panel redraw only what changed. */
    bool takeWrittenFrames (size_t& from, size_t& to) noexcept
    {
        if (dirtyTo <= dirtyFrom) return false;
        from = dirtyFrom; to = dirtyTo;
        dirtyFrom = static_cast<size_t> (-1); dirtyTo = 0;
        return true;
    }

    /** Device frames the tape's capture ring lost (the message thread fell behind). */
    size_t droppedFrames() const noexcept { return engine.tapeRing().droppedFrames(); }

    /** Previous's grace, in seconds. */
    static constexpr double kPreviousGraceSeconds = 0.75;

private:
    void drain();
    /** Bring the model's position up to the audio thread's playback. */
    void followPlayback();
    /** After a jump: replay from the new position, or stop if it ran off the end. */
    bool jumped (bool wasPlaying);

    Engine& engine;
    Tape model;
    TapeSource src;
    uint32_t gen = 0;
    bool stopping = false;      // a take ends: wait for the audio thread, then take the rest
    bool clearPending = false;  // blank the tape once playback has let go
    bool discardOnSettle = false;  // cleared mid-take: throw the last of it away
    bool scrubOn = false, playAfterScrub = false;
    int erasingTake = -1;       // a delete waits for the audio thread to let go
    uint32_t numCuts = 0;
    std::vector<float> bufL, bufR;
    size_t dirtyFrom = static_cast<size_t> (-1), dirtyTo = 0;
};

/** Send to canvas (11.3): the tape's frames [from, to) as a new Sound at `rate`,
    16 bit, two tracks (the left one routed left, the right one right), converted Raw
    (truncated) or Clean (dithered). Null when the span is empty. */
std::unique_ptr<SoundDocument> soundFromTape (const TapeStore& store, size_t from, size_t to, double rate,
                                              const std::string& name, ConvertMode mode = ConvertMode::raw);

} // namespace se
