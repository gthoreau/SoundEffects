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

#include "SoundDocument.h"
#include "SpscQueue.h"
#include "Transport.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

namespace se {

/** One Window: a UI document, its audio document, transport, and mixer channel. */
struct Window
{
    uint64_t id = 0;
    std::unique_ptr<SoundDocument> uiDoc;   // message-thread authoritative (draw + edit)
    SoundDocument*                 audioDoc = nullptr;   // audio-thread only; swapped
    std::unique_ptr<Transport>     transport;

    std::atomic<float> gain  { 1.0f };
    std::atomic<bool>  muted { false };

    // Post-gain peak of THIS Window's own contribution to the sum, published by
    // the audio thread for the mixer's VU meters. It falls back on its own (see
    // render()), so the UI can sample it at any rate and still see a decay.
    std::atomic<float> meter { 0.0f };

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
    const std::vector<uint64_t>& windowIds() const noexcept { return order; }
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
    bool seek (uint64_t id, size_t bytes);

    /** Free Windows/documents the audio thread has retired. Msg thread, periodic. */
    void collectRetired();

    // --- audio thread -------------------------------------------------------
    void render (float* left, float* right, int frames) noexcept;

    double sampleRate() const noexcept { return deviceRate; }

    static float dbToGain (float db) noexcept
    {
        return db <= -96.0f ? 0.0f : std::pow (10.0f, db * 0.05f);
    }

private:
    struct Command
    {
        enum class Kind : uint8_t { AddWindow, RemoveWindow, Transport, SwapDoc } kind {};
        enum class Tx   : uint8_t { Play, Stop, Pause, Advance, NormalSpeed, Seek } tx {};
        uint64_t        id = 0;
        Window*         window = nullptr;   // AddWindow: ownership transfers to audio
        SoundDocument*  doc = nullptr;      // SwapDoc: ownership transfers to audio
        PlayOptions     play {};
        size_t          seekBytes = 0;
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

    double deviceRate = 48000.0;
};

} // namespace se
