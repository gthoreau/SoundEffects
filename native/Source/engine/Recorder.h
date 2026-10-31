// Recorder — records the Engine's capture into a Sound. Message thread only; no JUCE.
//
// The audio thread captures the chosen source (Engine::startCapture) into a ring of
// float frames at the DEVICE's rate; this class drains the ring on a timer, converts
// it to the Sound's rate and depth (RecordConvert: Raw or Clean) and writes it into
// the Window's UI document. The document is published to the audio thread (one
// commitEdit) only when the recording pauses or stops, never per tick: the
// transport keeps playing the tracks that were there when it started, which is
// exactly what "Record is also Play" wants, and the tracks being written do not
// sound while they are written.
//
// Where it lands (the spec, "Audio Routing & Resampling"):
//   - NO selection: one new Track (incoming mono) or two (incoming stereo, the
//     first routed to the left output, the second to the right), appended after the
//     last track and starting at 0. A Sound whose tracks are all still empty gives
//     those up first rather than leaving blank lanes above the recording.
//     Pause, then Record again, appends to the same tracks.
//   - A selection over ONE track: the incoming audio is summed to mono (the
//     average, so a true mono source does not double).
//   - A selection over TWO tracks: incoming left on the upper one, right on the
//     lower one, whatever the tracks' speaker toggles say.
//   - THREE or more: not allowed (canRecord() is false).
//   With Loop off the recording is a punch-in/out: it starts at the selection's
//   start and ends by itself at its end. With Loop on it records round and round
//   inside the selection, over what was there, until stopped or paused.
//
// Latency: the audio thread stamps nothing; frame k of the capture is written to
// position k of the pass. The interface's round-trip delay is not compensated.

#pragma once

#include "Engine.h"
#include "RecordConvert.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace se {

struct RecordSetup
{
    CaptureSetup source;                 // where the audio comes from
    bool incomingStereo = true;          // 2 new tracks / L+R; false = 1 new track (a mono source)
    ConvertMode mode = ConvertMode::raw; // Raw (default) or Clean
    bool loop = false;                   // the Loop button: record round and round inside a selection
};

class Recorder
{
public:
    explicit Recorder (Engine& e) : engine (e) {}

    enum class State { idle, recording, paused, finishing };

    /** Can this Sound be recorded into right now: any selection that covers at most two tracks. */
    static bool canRecord (const SoundDocument& doc) noexcept;

    /** Begin a take in `window`. False when it cannot (3+ tracks selected, a take is
        already running, no such Window). */
    bool start (uint64_t window, const RecordSetup& setup);
    void pause();
    void resume();
    void stop();

    /** Message thread, regularly (30 Hz): drain, convert, write, and finish what is finishing. */
    void service();

    State state() const noexcept { return st; }
    bool active() const noexcept { return st != State::idle; }
    uint64_t window() const noexcept { return win; }
    /** Frames of the Sound's own rate written so far in this take. */
    size_t framesWritten() const noexcept { return written; }
    /** Device frames the ring dropped because the message thread fell behind. */
    size_t droppedFrames() const noexcept { return engine.captureRing().droppedFrames(); }
    /** The 0-based tracks this take writes into. */
    const std::vector<int>& targetTracks() const noexcept { return targets; }

    /** The frames of the target tracks written since the last call, as [from, to);
        false when none were. Lets the waveform redraw only what a tick recorded
        rather than re-read the whole take (a 10-minute take is 66 ms to re-read). */
    bool takeWrittenFrames (size_t& from, size_t& to) noexcept
    {
        if (dirtyTo <= dirtyFrom) return false;
        from = dirtyFrom; to = dirtyTo;
        dirtyFrom = static_cast<size_t> (-1); dirtyTo = 0;
        return true;
    }

    /** The take ended on its own (a punch-out reached the selection's end) or by stop(). */
    std::function<void (uint64_t window)> onFinished;
    /** The document changed under the Window (its waveform needs redrawing). */
    std::function<void (uint64_t window)> onChanged;

private:
    void drain (bool all);
    void write (const std::vector<int32_t>& l, const std::vector<int32_t>& r);
    void settle();          // finishing: the audio thread has stopped; take the rest, publish
    uint64_t mask() const noexcept;

    Engine& engine;
    State st = State::idle;
    State after = State::idle;   // where `finishing` goes: paused or idle
    uint64_t win = 0;
    RecordSetup setup;
    std::unique_ptr<RecordConverter> conv;

    enum class Where { newTracks, selection } where = Where::newTracks;
    std::vector<int> targets;    // 0-based track indices; [0] takes left, [1] right
    bool sumToMono = false;      // selection over one track
    size_t startFrame = 0;       // selection: first frame, in the Sound's frames
    size_t spanFrames = 0;       // selection: its length
    size_t cursor = 0;           // frames written into the take (selection: modulo the span when looping)
    size_t written = 0;
    size_t dirtyFrom = static_cast<size_t> (-1), dirtyTo = 0;   // see takeWrittenFrames()
    bool punchedOut = false;
    int waitTicks = 0;
    std::vector<float> bufL, bufR, tmpL, tmpR;
    std::vector<int32_t> outL, outR;
};

} // namespace se
