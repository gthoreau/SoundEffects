// Engine — the whole-desktop mixer AND the lock-free bridge between the UI
// (message thread) and audio rendering (audio thread).
//
// Playback is GLOBAL and ADDITIVE across every open Window; per-Window level/mute
// is the main mixer. See the loop/mixer notes elsewhere.
//
// ## Threading model
//
// The AUDIO THREAD is the sole owner of the *live* Window set and of each
// Window's AUDIO document (`audioDoc`). The MESSAGE THREAD never touches those; it
//   - posts commands (add / remove / transport / swap-doc) into a lock-free SPSC
//     queue, drained at the top of render(),
//   - owns each Window's UI document (`uiDoc`) which it freely draws and edits,
//   - reads/writes per-Window mixer atomics (gain / muted).
//
// ## RCU editing (edits during playback, without glitches)
//
// To edit, the UI mutates `uiDoc` (single-threaded — simple), then commitEdit()
// publishes an immutable clone to the audio thread via a SwapDoc command; the
// audio thread swaps its `audioDoc` pointer (O(1)) and hands the OLD doc back
// through a retirement queue to be freed on the message thread. So the audio
// thread never allocates, never blocks, and never sees a half-edited buffer.
//
// Precondition: all message-thread methods are called from ONE thread (single
// producer). render() is the single consumer. No JUCE here.

#pragma once

#include "CaptureRing.h"
#include "SoundDocument.h"
#include "SpscQueue.h"
#include "TapeStore.h"
#include "Transport.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

namespace se {

/** Where a recording takes its audio from (Select Audio Source). */
enum class AudioSourceKind : uint8_t { external, canvas, daw };

/** What the audio thread captures into the CaptureRing while a capture runs. */
struct CaptureSetup
{
    AudioSourceKind kind = AudioSourceKind::external;
    /** canvas: 0 = the main mix, otherwise that Window's own post-fader output. */
    uint64_t tapWindow = 0;
    /** canvas main mix: this Window (the one being recorded into) is left out of it. */
    uint64_t excludeWindow = 0;
    /** external: the first device input channel; with `stereo` it is a pair. */
    int  firstChannel = 0;
    bool stereo = true;
};

/** What the Magnetophon's tape records from (0.5.0): a second capture path, with
    its own ring, independent of the Recorder's. Stereo float at the device rate. */
struct TapeSource
{
    /** canvas = the Main output (after Main Level) or, with `tapWindow`, one Sound's
        own post-fader output; external = device inputs; daw = the host's input pair. */
    AudioSourceKind kind = AudioSourceKind::canvas;
    uint64_t tapWindow = 0;     // canvas: 0 = Main
    int  firstChannel = 0;      // external: the first device input channel
    bool stereo = true;         // external: a pair; false = one channel on both sides
};

/** One Window: a UI document, its audio document, transport, and mixer channel. */
struct Window
{
    uint64_t id = 0;
    std::unique_ptr<SoundDocument> uiDoc;   // message-thread authoritative (draw + edit)
    SoundDocument*                 audioDoc = nullptr;   // audio-thread only; swapped
    std::unique_ptr<Transport>     transport;

    std::atomic<float> gain  { 1.0f };
    std::atomic<bool>  muted { false };

    // Solo is CUMULATIVE and a filter rather than a flag: if ANY Window is soloed,
    // only the soloed ones sound. Mute still wins over solo on the same Window — an
    // explicit "off" outranks an implicit "on", so soloing does not resurrect a
    // channel you deliberately shut.
    //
    // None of gain / muted / soloed stops a Window's transport: an unheard channel
    // is still rendered and thrown away, so it stays in phase with the ones you can
    // hear. See render().
    std::atomic<bool>  soloed { false };

    // Stereo balance, -1 (hard left) .. 0 (centre) .. +1 (hard right). Linear and
    // one-sided: unity at centre, and moving toward one side only attenuates the
    // OTHER side, down to silence at the end stop. See balanceGains().
    std::atomic<float> balance { 0.0f };

    // Playback direction (a PlayDirection), the Window's own. The message thread's
    // copy, for the Play button's icon and the Transport menu's ticks; the audio
    // thread has its own in `transport`, set by the command setDirection() posts.
    std::atomic<uint8_t> direction { static_cast<uint8_t> (PlayDirection::forward) };

    // Post-gain, post-balance peaks of THIS Window's own contribution to the sum,
    // one per output side, published by the audio thread for the mixer's stereo
    // VU. They fall back on their own (see render()), so the UI can sample them at
    // any rate and still see a decay.
    std::atomic<float> meterLeft  { 0.0f };
    std::atomic<float> meterRight { 0.0f };

    // Play / Stop posted and applied, for Engine::isRolling(): `sent` and `lastPlay`
    // are the message thread's, `applied` the audio thread's answer.
    uint32_t playStopSent = 0;
    bool     lastSentPlay = false;
    std::atomic<uint32_t> playStopApplied { 0 };

    // Audio thread only: removed, kept until its sound has faded out.
    bool leaving = false;
    // Audio thread only: gain and balance (what the capture taps hear), and the
    // mute / solo gate on top of them in the mix, each ramped to where it is set.
    Ramp faderLeft, faderRight, gate;
    bool rampsSet = false;   // a Window's first block starts where it is set

    // Canvas placement, in canvas ("world") coordinates. Zero means "not placed
    // yet" — the canvas picks a spot and a size the first time it sees the Window.
    // `width` is the width the Window ASKS for; in the grid the whole column shares
    // one width, which is whatever the last grow-box drag in it asked for.
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;

    /** Cell address of a Window the canvas has not placed in the grid yet. The grid
        grows in every direction, so a real column index may be negative — "unset"
        cannot just be a negative number. */
    static constexpr int kNoCell = std::numeric_limits<int>::min();

    // Cell address in the canvas grid, and index into the canvas tint palette
    // (tint < 0 = unpainted).
    int gridCol = kNoCell, gridRow = 0, tint = -1;

    /** The mixer slot the Window's strip sits in, 0-based (the panel shows it
        1-based). kNoSlot = not seated yet; the mixer gives it the lowest free one.
        Message-thread data, like the placement above. */
    static constexpr int kNoSlot = -1;
    int slot = kNoSlot;

    /** The Window's asset id: what its WAVs in a project's Assets/ folder are
        numbered by (`<assetId> <name> - track T.wav`). Given once by addWindow from
        the canvas's counter and never reused, so neither a mixer reorder nor closing
        another Sound renames a file. 0 = none (never seen on an open Window). */
    uint32_t assetId = 0;

    ~Window() { delete audioDoc; }
};

class Engine
{
public:
    Engine() = default;
    ~Engine();

    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    static constexpr size_t kMaxWindows = 256;

    void prepare (double deviceRate, int maxBlockSize);
    /** The length of every ramp (Declick.h); 0 switches them off, for tests that read
        exact samples. Before audio runs. */
    void setDeclickMs (double ms) noexcept
    {
        declickMs = ms;
        for (auto& kv : registry) kv.second->transport->setDeclickMs (ms);
    }

    // --- message thread: structure ------------------------------------------
    /** `assetId` 0 = take the next one from the canvas's counter; a saved id is
        kept as it is, and the counter moves past it. */
    uint64_t addWindow (std::unique_ptr<SoundDocument> doc, uint32_t assetId = 0);
    bool     removeWindow (uint64_t id);
    size_t   numWindows() const noexcept { return order.size(); }
    /** Is ANY Window soloed — i.e. is the solo filter on at all? The same question
        render() asks before it mixes; the mixer asks it to grey the channels solo
        is silencing. */
    bool     anyWindowSoloed() const noexcept;
    /** ⌘-click on a lit M: unmute every Window. ⌘-click on a lit S: clear every solo. */
    void     unmuteAll() noexcept;
    void     clearAllSolos() noexcept;
    /** A click on a Window's S box (Mixer strip, title bar). Plain: toggle it. With ⌘
        on a lit box: clear every solo. With ⌘ on an unlit box: the solo swap, which
        leaves that Window as the only one soloed. */
    void     clickSolo (uint64_t id, bool command) noexcept;
    const std::vector<uint64_t>& windowIds() const noexcept { return order; }

    /** The canvas's asset-id counter: the id the next new Window gets. Saved in the
        .sefx (v4) so ids are never handed out twice for one canvas. */
    uint32_t nextAssetId() const noexcept { return assetCounter; }
    void     setNextAssetId (uint32_t next) noexcept { assetCounter = next < 1 ? 1 : next; }

    // --- message thread: mixer slots ----------------------------------------
    // Every Window sits in a numbered mixer slot (Window::slot), always 0..N-1 with
    // no gaps (decision of 2026-10-31): addWindow seats a new one last; removing a
    // Window closes the gap, the ones below moving up.
    int      lowestFreeSlot() const;
    /** The Window seated in `slot`, or 0. */
    uint64_t windowInSlot (int slot) const noexcept;
    /** The highest occupied slot, or Window::kNoSlot when there is no Window. */
    int      highestSlot() const noexcept;
    /** Move a Window to `slot` by INSERTING it there: the others close up behind it
        and those from `slot` on move down one. Past the end = last. */
    bool     moveToSlot (uint64_t id, int slot);
    /** Resolve duplicate and missing slots (a damaged or a v1 file), then close any
        gaps. */
    void     normaliseSlots();
    Window*  windowById (uint64_t id) noexcept;

    // --- message thread: editing (RCU) --------------------------------------
    /** The UI-authoritative document to mutate. nullptr if unknown. */
    SoundDocument* editDoc (uint64_t id) noexcept;
    /** Publish the current uiDoc to the audio thread. Call after mutating editDoc.
        Returns false if the command queue is full. */
    bool commitEdit (uint64_t id);
    /** Publish ONLY the selection, the Set Loop region and each track's output
        routing — what a click, a drag or the Selection menu changes — without
        cloning a single sample (perf audit P3: commitEdit copies the whole Sound).
        The audio thread writes them into its own document in place. Only for
        changes that touch nothing else: every sample or track edit must still go
        through commitEdit. Falls back to commitEdit past kPhaseTracks tracks. */
    bool commitMeta (uint64_t id);

    // --- message thread: transport (applied on the audio thread, in order) ---
    bool play  (uint64_t id, const PlayOptions& opts = {});
    bool stop  (uint64_t id);
    /** Message thread: is it playing, counting a Play or Stop the audio thread has
        not applied yet? A key let go and struck again within one block must not take
        its own stopping Window for a rolling one. */
    bool isRolling (uint64_t id) noexcept;
    bool pause (uint64_t id);
    bool advance (uint64_t id);
    bool normalSpeed (uint64_t id);
    /** Re-pitch a ROLLING transport without restarting it. See Transport. */
    bool setRateScale (uint64_t id, double scale);
    bool seek (uint64_t id, size_t bytes);
    /** Forward / Backward / Ping-pong for one Window, at any time: a rolling
        transport re-anchors every track's phase and carries on, no restart. */
    bool setDirection (uint64_t id, PlayDirection d);
    PlayDirection direction (uint64_t id) noexcept;

    // --- message thread: recording ------------------------------------------
    // Record is also a Play: the transport plays what is already there (the tracks
    // in `excludeMask` stay silent, they are being written) and the capture starts
    // in the very same block, so the two stay in step. Recording forces Forward.
    // The Recorder (Recorder.h) is the one that drives these.
    bool recordStart  (uint64_t id, const CaptureSetup& setup, const PlayOptions& play, uint64_t excludeMask);
    bool recordPause  (uint64_t id);
    bool recordResume (uint64_t id, const PlayOptions& play, uint64_t excludeMask);
    bool recordStop   (uint64_t id);
    /** Start a take on a transport that is ALREADY rolling or paused, without
        restarting it (0.5.0: Record armed while Play is on, ⇧R while playing). It
        turns Forward with the usual re-anchor and a paused one resumes; a stopped
        one plays with `play`. The frame it joins at is published (captureStartFrame),
        read from track `refTrack` (0-based) or the topmost one that is reading. */
    bool recordJoin   (uint64_t id, const CaptureSetup& setup, const PlayOptions& play, uint64_t excludeMask, int refTrack);
    /** End the capture only: the transport carries on as it is (disarm mid-take).
        The tracks it wrote stay silent until the transport next plays or stops. */
    bool recordEnd    (uint64_t id);
    /** After recordEnd, once the take is published (commitEdit): its tracks sound
        again, faded in, without waiting for the next Play or Stop. */
    bool recordRelease (uint64_t id);
    /** Bumped by the audio thread each time a take begins (recordStart / recordJoin),
        after captureStartFrame() is set for it. */
    uint32_t captureStartGen() const noexcept { return capStartGen.load (std::memory_order_acquire); }
    /** The Sound frame the last take began on (see recordJoin; 0 for recordStart). */
    size_t captureStartFrame() const noexcept { return capStartFrame.load (std::memory_order_relaxed); }

    /** Free Windows/documents the audio thread has retired. Msg thread, periodic. */
    void collectRetired();

    // --- audio input / capture (0.4.0 recording) ----------------------------
    // The audio thread captures the chosen source into captureRing(); the Recorder
    // (message thread) drains it. Capturing is independent of any transport.
    bool startCapture (const CaptureSetup& setup);
    bool stopCapture();
    /** True from the block the start command lands in until the block the stop does:
        the audio thread's acknowledgement, which the Recorder waits for before it
        trusts that nothing more will arrive. */
    bool captureRunning() const noexcept { return capRunning.load (std::memory_order_acquire); }
    CaptureRing& captureRing() noexcept { return ring; }

    // --- Magnetophon (0.5.0): the tape's own capture and playback --------------
    // A second capture path, independent of the Recorder's: its own source, gain,
    // monitor and ring, so the tape can run while a Sound records. The Main source is
    // what the speakers get after Main Level, WITHOUT the tape's own playback, which
    // is added after every capture: the tape never re-records itself, and neither
    // does the Recorder's Main Mix. Tape playback goes through Main Level too.
    // The TapeDeck (Tape.h) drives all of this.
    // Its playback fades in, out and across every jump like a Sound's (Declick.h);
    // the capture is never ramped.
    bool tapeCaptureStart (const TapeSource& source);
    bool tapeCaptureStop();
    /** The audio thread's acknowledgement, as captureRunning() is the Recorder's. */
    bool tapeCaptureRunning() const noexcept { return tapeCapRunning.load (std::memory_order_acquire); }
    CaptureRing& tapeRing() noexcept { return tapeRingBuf; }
    void  setTapeInputGain (float g) noexcept { tapeGainValue.store (g, std::memory_order_relaxed); }
    float tapeInputGain() const noexcept { return tapeGainValue.load (std::memory_order_relaxed); }
    /** Hear an external or DAW source while the tape records it (off by default). */
    void setTapeMonitor (bool on) noexcept { tapeMonitorOn.store (on, std::memory_order_relaxed); }
    bool tapeMonitor() const noexcept { return tapeMonitorOn.load (std::memory_order_relaxed); }

    TapeStore& tapeStore() noexcept { return tape; }
    /** Play the tape from `frame` to its end (a seek when it already plays). `gen`
        tags the command: tapeAckGen() says the audio thread has applied it. */
    bool tapePlay (size_t frame, uint32_t gen);
    bool tapeStop (uint32_t gen);
    /** Scrub (the panel's strip dragged by hand): the audio thread chases the target
        frame like a tape pulled past the head, varispeed, with an inertia of ~50 ms;
        its level follows its speed, so a tape at rest is silent. Start (or restart)
        at `frame`, move the target as the mouse moves, then let go: it fades out as
        the tape comes to rest. Playback stops while scrubbing. */
    bool tapeScrub (size_t frame, uint32_t gen);
    void tapeScrubTo (size_t frame) noexcept { tapeScrubTarget.store (frame, std::memory_order_relaxed); }
    bool tapeScrubEnd (uint32_t gen);
    /** The audio thread is reading the tape (playing or scrubbing). */
    bool tapePlaying() const noexcept { return tapePlayingFlag.load (std::memory_order_acquire); }
    /** The frame the tape's playback will read next (published once a block). */
    size_t tapePlayFrame() const noexcept { return tapePlayPos.load (std::memory_order_relaxed); }
    uint32_t tapeAckGen() const noexcept { return tapeAck.load (std::memory_order_acquire); }
    /** Peaks of what the tape source delivers while it records, for the panel. */
    float tapeMeter() const noexcept { return tapeMeterValue.load (std::memory_order_relaxed); }

    // --- Main Level (0.5.0): the master fader over the whole mix --------------
    // Linear, applied to the summed Windows BEFORE the final clamp (and before the
    // Canvas capture, so the Main source hears what the speakers get). Never stops
    // or re-aims a transport: it only scales what the mix already carries.
    // Saved with the canvas (state v5); 1 = unity.
    static constexpr float kMaxMasterGain = 4.0f;   // +12 dB, far past the fader's +6
    void  setMasterGain (float g) noexcept
    {
        masterGainValue.store (std::isfinite (g) ? std::clamp (g, 0.0f, kMaxMasterGain) : 1.0f,
                               std::memory_order_relaxed);
    }
    float masterGain() const noexcept { return masterGainValue.load (std::memory_order_relaxed); }
    /** Main Mute (0.6.0), the M beside Main Level: silences everything that reaches
        the output, input monitoring included. Ramped like the fader,
        which keeps its value. Never saved: a canvas always opens unmuted. Like Main
        Level it comes before the Canvas capture, and it stops no transport. */
    void setMasterMuted (bool m) noexcept { masterMutedValue.store (m, std::memory_order_relaxed); }
    bool masterMuted() const noexcept { return masterMutedValue.load (std::memory_order_relaxed); }
    /** Peaks of the final output (after Main Level and the clamp), one per side, for
        the Main Level's VU. Published by the audio thread and decayed there like the
        Windows' own. */
    float masterMeterLeft() const noexcept  { return masterMeterL.load (std::memory_order_relaxed); }
    float masterMeterRight() const noexcept { return masterMeterR.load (std::memory_order_relaxed); }

    /** Gain on the captured signal (and on what is monitored), linear. */
    void  setInputGain (float g) noexcept { inputGainValue.store (g, std::memory_order_relaxed); }
    float inputGain() const noexcept { return inputGainValue.load (std::memory_order_relaxed); }
    /** Monitoring (hear the input through the output) is OFF by default, against
        feedback. It only applies to a device or DAW source: the Canvas is audible
        already. */
    void setMonitor (bool on) noexcept { monitorOn.store (on, std::memory_order_relaxed); }
    bool monitor() const noexcept { return monitorOn.load (std::memory_order_relaxed); }

    // --- audio thread -------------------------------------------------------
    /** `inputs` are the device's input channels (planar, `numInputs` of them), read
        BEFORE the output is written: they must not alias `left`/`right`. */
    void render (float* left, float* right, int frames,
                 const float* const* inputs = nullptr, int numInputs = 0) noexcept;

    double sampleRate() const noexcept { return deviceRate; }

    static float dbToGain (float db) noexcept
    {
        return db <= -96.0f ? 0.0f : std::pow (10.0f, db * 0.05f);
    }

    /** Left and right multipliers for a balance in -1..+1: unity at centre, the
        opposite side scaled down linearly to 0 at the end stop. */
    struct BalanceGains { float left = 1.0f, right = 1.0f; };
    static BalanceGains balanceGains (float balance) noexcept
    {
        const float b = std::clamp (balance, -1.0f, 1.0f);
        return { b > 0.0f ? 1.0f - b : 1.0f, b < 0.0f ? 1.0f + b : 1.0f };
    }

private:
    /** What commitMeta() carries: plain values, so the Command stays trivially copyable. */
    struct DocMeta
    {
        size_t   selSt = 0, selEnd = 0, loopSt = 0, loopEnd = 0;
        int      firstSelTrack = 1, lastSelTrack = 1;
        int      numTracks = 0;              // must match the audio document's, or it is ignored
        uint64_t outLeft = 0, outRight = 0;  // bit t = track t routed to that side
    };

    struct Command
    {
        enum class Kind : uint8_t { AddWindow, RemoveWindow, Transport, SwapDoc, SetMeta,
                                    CaptureStart, CaptureStop,
                                    TapeCaptureStart, TapeCaptureStop, TapePlay, TapeStop,
                                    TapeScrub, TapeScrubEnd } kind {};
        enum class Tx   : uint8_t { Play, Stop, Pause, Advance, NormalSpeed, Seek,
                                    RateScale, Direction, RecordStart, RecordPause,
                                    RecordResume, RecordStop, RecordJoin, RecordEnd, RecordRelease } tx {};
        uint64_t        id = 0;
        Window*         window = nullptr;   // AddWindow: ownership transfers to audio
        SoundDocument*  doc = nullptr;      // SwapDoc: ownership transfers to audio
        PlayOptions     play {};
        size_t          seekBytes = 0;
        PlayDirection   direction = PlayDirection::forward;
        CaptureSetup    capture {};
        uint64_t        excludeMask = 0;
        int             refTrack = -1;      // RecordJoin
        DocMeta         meta {};            // SetMeta
        TapeSource      tape {};            // TapeCaptureStart
        uint32_t        gen = 0;            // TapePlay / TapeStop; Play / Stop
    };

    bool post (const Command& c) noexcept { return incoming.push (c); }
    bool postPlayStop (Command& c, bool isPlay) noexcept;

    void drainCommands() noexcept;
    /** Hand the live Window at `index` back to the message thread. Audio thread. */
    void retire (size_t index) noexcept;
    /** The tape's playback is about to stop or jump: what it would have played next
        is copied aside, fading out, so nothing reads the tape afterwards (a take may
        be about to record over it, or Clear to free it). Audio thread. */
    void fadeOutTape() noexcept;
    Window* liveById (uint64_t id) noexcept;
    void applyTransport (Window& w, const Command& c) noexcept;

    // --- message-thread state ----------------------------------------------
    std::unordered_map<uint64_t, Window*> registry;   // non-owning; UI's view
    std::vector<uint64_t> order;                       // stable creation order

    /** The seated Windows in slot order (ties in creation order). */
    std::vector<Window*> seatedInSlotOrder() const;
    /** Renumber the seated Windows 0..N-1, keeping their order. */
    void compactSlots();
    uint64_t nextId = 1;
    uint32_t assetCounter = 1;

    // --- shared, lock-free --------------------------------------------------
    SpscQueue<Command, 1024>       incoming;    // message -> audio
    SpscQueue<Window*, 1024>       retiredWins; // audio -> message
    SpscQueue<SoundDocument*, 1024> retiredDocs; // audio -> message (swapped-out docs)

    // --- audio-thread state -------------------------------------------------
    std::vector<Window*> live;
    std::vector<float> scratchL, scratchR;
    std::vector<float> capL, capR, tapL, tapR, exL, exR;   // capture blocks
    CaptureSetup cap {};
    bool capActive = false;

    // --- shared, lock-free: input / capture ---------------------------------
    CaptureRing ring;
    std::atomic<float> inputGainValue { 1.0f };
    std::atomic<float> masterGainValue { 1.0f };
    std::atomic<float> masterMeterL { 0.0f }, masterMeterR { 0.0f };
    Ramp masterRamp;              // audio thread only: Main Level, Main Mute in it
    std::atomic<bool> masterMutedValue { false };
    Ramp muteRamp;                // audio thread only: Main Mute alone, for the monitors
    std::atomic<bool>  monitorOn { false };
    std::atomic<bool>  capRunning { false };
    std::atomic<uint32_t> capStartGen { 0 };
    std::atomic<size_t>   capStartFrame { 0 };

    // --- Magnetophon ----------------------------------------------------------
    TapeStore tape;
    CaptureRing tapeRingBuf;
    std::vector<float> tcapL, tcapR, ttapL, ttapR, tplayL, tplayR;   // audio thread blocks
    TapeSource tapeCap {};          // audio thread
    bool tapeCapActive = false;     // audio thread
    bool tapePlayActive = false;    // audio thread
    size_t tapePos = 0;             // audio thread: next frame to play
    float  tapeLevel = 0.0f;        // audio thread: the playback's gain, rising to 1
    // audio thread: the fade-out copied aside by fadeOutTape(), [tfadeAt, tfadeEnd)
    // still to be heard; tsnap is its reading room.
    std::vector<float> tfadeL, tfadeR, tsnapL, tsnapR;
    size_t tfadeAt = 0, tfadeEnd = 0;
    bool   scrubActive = false, scrubReleasing = false;   // audio thread
    double scrubPos = 0.0, scrubVel = 0.0;                 // frames, frames per output frame
    float  scrubGain = 0.0f;
    double scrubVelCoeff = 0.0, scrubGainCoeff = 0.0;      // per-sample one-pole coefficients
    std::atomic<size_t> tapeScrubTarget { 0 };
    std::atomic<bool> tapeCapRunning { false };
    std::atomic<bool> tapePlayingFlag { false };
    std::atomic<size_t> tapePlayPos { 0 };
    std::atomic<uint32_t> tapeAck { 0 };
    std::atomic<float> tapeGainValue { 1.0f };
    std::atomic<bool> tapeMonitorOn { false };
    std::atomic<float> tapeMeterValue { 0.0f };

    double deviceRate = 48000.0;
    double declickMs = kDeclickMs;
};

} // namespace se
