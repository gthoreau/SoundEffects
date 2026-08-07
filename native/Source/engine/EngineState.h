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
};

/** The whole canvas, in creation order. */
struct EngineState
{
    std::vector<WindowState> windows;
};

/** Current format version written by serializeState(). Readers accept <= this. */
constexpr uint32_t kEngineStateVersion = 1;

// --- blob <-> state (pure; no Engine involved) ------------------------------

std::vector<uint8_t> serializeState (const EngineState& state);

/** Decode a blob. Returns false — leaving `out` untouched — on anything it does
    not recognise, including truncation and lengths a short blob cannot back. */
bool parseEngineState (const void* data, size_t sizeBytes, EngineState& out);

// --- Engine <-> state (message thread only) ---------------------------------

/** Snapshot every open Window from its UI document and mixer atomics. */
EngineState captureEngineState (Engine& engine);

/** Replace the canvas with `state`: closes every open Window, then opens one per
    saved Window. Returns false if the engine refused a command (queue full or the
    Window limit), having applied as much as it could. */
bool applyEngineState (Engine& engine, const EngineState& state);

/** captureEngineState + serializeState. */
inline std::vector<uint8_t> serializeEngine (Engine& engine)
{
    return serializeState (captureEngineState (engine));
}

} // namespace se
