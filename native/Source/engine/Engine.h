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

/** One Window: a UI document, its audio document, transport, and mixer channel. */
struct Window
{
    uint64_t id = 0;
    std::unique_ptr<SoundDocument> uiDoc;   // message-thread authoritative (draw + edit)
    SoundDocument*                 audioDoc = nullptr;   // audio-thread only; swapped
    std::unique_ptr<Transport>     transport;

    std::atomic<float> gain  { 1.0f };
    std::atomic<bool>  muted { false };

    // Solo is CUMULATIVE and, like the per-track solo in Mixer.h, a filter rather
    // than a flag: if ANY Window is soloed, only the soloed ones sound. Mute still
    // wins over solo on the same Window — an explicit "off" outranks an implicit
    // "on", so soloing does not resurrect a channel you deliberately shut.
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

    // --- message thread: structure ------------------------------------------
    uint64_t addWindow (std::unique_ptr<SoundDocument> doc);
    bool     removeWindow (uint64_t id);
    size_t   numWindows() const noexcept { return order.size(); }
    /** Is ANY Window soloed — i.e. is the solo filter on at all? The same question
        render() asks before it mixes; the mixer asks it to grey the channels solo
        is silencing. */
    bool     anyWindowSoloed() const noexcept;
    const std::vector<uint64_t>& windowIds() const noexcept { return order; }

    // --- message thread: mixer slots ----------------------------------------
    // Every Window sits in a numbered mixer slot (Window::slot). addWindow seats a
    // new one in the lowest free slot; removing a Window frees its slot, leaving a
    // gap the next new Window fills.
    int      lowestFreeSlot() const;
    /** The Window seated in `slot`, or 0. */
    uint64_t windowInSlot (int slot) const noexcept;
    /** The highest occupied slot, or Window::kNoSlot when there is no Window. */
    int      highestSlot() const noexcept;
    /** Seat a Window in `slot`; whoever was there takes its old slot. */
    bool     moveToSlot (uint64_t id, int slot);
    /** Resolve duplicate and missing slots (a damaged or a v1 file). */
    void     normaliseSlots();
    Window*  windowById (uint64_t id) noexcept;

    // --- message thread: editing (RCU) --------------------------------------
    /** The UI-authoritative document to mutate. nullptr if unknown. */
    SoundDocument* editDoc (uint64_t id) noexcept;
    /** Publish the current uiDoc to the audio thread. Call after mutating editDoc.
        Returns false if the command queue is full. */
    bool commitEdit (uint64_t id);

    // --- message thread: transport (applied on the audio thread, in order) ---
    bool play  (uint64_t id, const PlayOptions& opts = {});
    bool stop  (uint64_t id);
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
    struct Command
    {
        enum class Kind : uint8_t { AddWindow, RemoveWindow, Transport, SwapDoc,
                                    CaptureStart, CaptureStop } kind {};
        enum class Tx   : uint8_t { Play, Stop, Pause, Advance, NormalSpeed, Seek,
                                    RateScale, Direction, RecordStart, RecordPause,
                                    RecordResume, RecordStop } tx {};
        uint64_t        id = 0;
        Window*         window = nullptr;   // AddWindow: ownership transfers to audio
        SoundDocument*  doc = nullptr;      // SwapDoc: ownership transfers to audio
        PlayOptions     play {};
        size_t          seekBytes = 0;
        PlayDirection   direction = PlayDirection::forward;
        CaptureSetup    capture {};
        uint64_t        excludeMask = 0;
    };

    bool post (const Command& c) noexcept { return incoming.push (c); }

    void drainCommands() noexcept;
    Window* liveById (uint64_t id) noexcept;
    void applyTransport (Window& w, const Command& c) noexcept;

    // --- message-thread state ----------------------------------------------
    std::unordered_map<uint64_t, Window*> registry;   // non-owning; UI's view
    std::vector<uint64_t> order;                       // stable creation order
    uint64_t nextId = 1;

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
    std::atomic<bool>  monitorOn { false };
    std::atomic<bool>  capRunning { false };

    double deviceRate = 48000.0;
};

} // namespace se
