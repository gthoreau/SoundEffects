// EngineState — the canvas, written down.
//
// What the host calls "plugin state" is, for this program, the user's work: every
// open Window, where it sits on the canvas, what colour it wears, and the actual
// sound inside it. So this is a document format in all but name, and it is pure
// engine — no JUCE — for the same reason the rest of `engine/` is: the standalone
// and the VST3/AU must persist the same bytes, and the round trip must be testable
// without a host.
//
// ## Three steps, deliberately separate
//
//   serializeEngine()  message thread. Reads each Window's UI document (`uiDoc`),
//                      which the message thread owns, plus the mixer atomics.
//   parseEngineState() ANY thread, touches nothing but the blob. A host may call
//                      setStateInformation off the message thread, and parsing is
//                      where the untrusted bytes are.
//   applyEngineState() message thread ONLY. Goes through Engine's command queue
//                      (removeWindow / addWindow), never at the live Window set —
//                      that belongs to the audio thread.
//
// Splitting parse from apply is what lets the processor decode wherever it was
// called and then marshal only the apply onto the message thread.
//
// ## Not saved, on purpose
//
// Transport position and play state: a restored canvas comes up stopped. The
// engine's Window ids are not saved either — they are per-session handles, and
// loading mints fresh ones.
//
// NB: this is OUR blob, not an interchange format. Byte-accurate AIFF/WAV (the
// extended-80 rate, offset-binary payload) is a separate port; see Import.h.

#pragma once

#include "Engine.h"
#include "SoundDocument.h"
#include "Track.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace se {

/** One Window's worth of saved state: its document, its placement, its channel. */
struct WindowState
{
    // --- the sound (SoundDocument) ------------------------------------------
    std::string name;
    double rateHz   = 22254.5454;
    int    bps      = 8;
    int    baseFreq = 60;
    std::vector<Track> tracks;

    size_t selSt = 0, selEnd = 0;
    int    firstSelTrack = 1, lastSelTrack = 1;
    size_t loopSt = 0, loopEnd = 0;
    std::vector<Chunk> chunks;

    // --- the Window on the canvas -------------------------------------------
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
    int   gridCol = Window::kNoCell, gridRow = 0, tint = -1;

    // --- its mixer channel ---------------------------------------------------
    float gain   = 1.0f;
    bool  muted  = false;
    bool  soloed = false;
    float balance = 0.0f;              // since v2
    int   slot    = Window::kNoSlot;   // since v2: 0-based mixer slot

    uint8_t direction = 0;             // since v3: a PlayDirection (0 forward, 1 backward, 2 ping-pong)

    /** Where each Track's audio lives when the file keeps it outside the .sefx (a
        project folder, v3): a path relative to the .sefx and the exact byte length.
        Empty for embedded audio. `tracks[i].data` is filled by the project reader. */
    std::vector<std::string> assetPaths;
    std::vector<uint64_t>    assetBytes;
};

/** The whole canvas, in creation order. */
struct EngineState
{
    std::vector<WindowState> windows;

    /** Written by serializeState: true = every Track's audio is a reference into
        the project's Assets/ folder (`assetPaths`), false = embedded in the blob.
        Set by parseEngineState from the file. A true state straight out of the
        parser has EMPTY track data until the project reader has loaded the assets. */
    bool externalAudio = false;

    /** Set by parseEngineState: the version the blob was written as (0 if built in memory). */
    uint32_t sourceVersion = 0;
};

/** Current format version written by serializeState(). Readers accept <= this.
    v2 (0.4.0) appends each Window's balance and mixer slot.
    v3 (0.4.0) adds a header byte (audio embedded / in Assets/ as WAVs) and appends each
    Window's playback direction. */
constexpr uint32_t kEngineStateVersion = 3;

/** The extension a saved canvas wears on disk. Lives here, beside the magic and
    the version, because it is part of the same identity: a file called .sefx is
    promising 'SEFX' + a version this reader accepts. */
constexpr const char* kEngineStateFileSuffix = ".sefx";

// --- blob <-> state (pure; no Engine involved) ------------------------------

/** With `state.externalAudio` the Tracks are written as references (`assetPaths` /
    `assetBytes` must be filled, one per Track) and their data is left out. */
std::vector<uint8_t> serializeState (const EngineState& state);

/** Decode a blob. Returns false — leaving `out` untouched — on anything it does
    not recognise, including truncation and lengths a short blob cannot back. */
bool parseEngineState (const void* data, size_t sizeBytes, EngineState& out);

// --- Engine <-> state (message thread only) ---------------------------------

/** One Window's state (Export Sound). False if `id` is not open. Message thread only. */
bool captureWindowState (Engine& engine, uint64_t id, WindowState& out);

/** Snapshot every open Window from its UI document and mixer atomics. */
EngineState captureEngineState (Engine& engine);

/** Replace the canvas with `state`: closes every open Window, then opens one per
    saved Window. Returns false if the engine refused a command (queue full or the
    Window limit), having applied as much as it could. */
bool applyEngineState (Engine& engine, const EngineState& state);

/** Add the Windows of `state` to the canvas WITHOUT touching what is open (Import
    Sound). Placement is dropped so the canvas places them, each lands in the first
    free mixer slot, solo is cleared (an import must not silence the canvas), and
    playback direction is kept. Returns false if the engine refused any. Message thread only. */
bool importEngineState (Engine& engine, const EngineState& state);

/** captureEngineState + serializeState. */
inline std::vector<uint8_t> serializeEngine (Engine& engine)
{
    return serializeState (captureEngineState (engine));
}

} // namespace se
