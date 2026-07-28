// EffectRunner — applying a destructive effect to a Window on the canvas.
//
// This is the bridge between the effects layer (which knows only about a
// SoundDocument) and the Engine (which owns Windows and the audio thread). It is
// the only place that knows an effect can spawn whole new documents, and the only
// place that decides what "apply to this Window" means.
//
// ## Why this is safe while audio is playing
//
// It rides the Engine's existing RCU editing path: mutate the UI-authoritative
// `editDoc`, then `commitEdit` publishes an immutable clone to the audio thread,
// which swaps a pointer and hands the old document back to be freed on the message
// thread. The audio thread never sees a half-processed buffer.
//
// ## Scope
//
// The panel's rules map onto the reference's own semantics almost exactly:
//   - audio selected inside the Window → the effect applies to that selection;
//   - Window selected, nothing selected inside → the whole Window.
//
// The second is NOT special-cased here. An empty selection has always meant
// everything (Gain.c), so `expandEmptySelection` in the skeleton does it. The one
// addition is the TRACK range: a caret leaves firstSelTrack/lastSelTrack wherever
// they were, and "all the audio in the Window" means all of its tracks.
//
// No JUCE — the UI calls in, this does not call out.

#pragma once

#include "Module.h"
#include "Results.h"
#include "Settings.h"
#include "engine/Engine.h"

#include <string>
#include <vector>

namespace se {

/** Why an effect cannot run right now — what the panel greys out and explains. */
enum class Applicability
{
    Ok,
    NoWindow,          // nothing selected on the canvas
    NoAudio,           // the Window has tracks but no samples in them
    NeedsTwoTracks,    // Difference, Sum, Iterative Crossfade
    NeedsExactTracks,  // some other exact-track requirement
};

/** A human sentence for the panel to show. Empty for Ok. */
const char* applicabilityText (Applicability, const Requirements&);

/** What an effect would act on if applied now — the readout beside Apply. There
    is no undo (§1), so this is shown BEFORE the irreversible thing happens. */
struct Scope
{
    bool   wholeWindow = true;   // no time selection: the effect takes everything
    int    firstTrack = 1;
    int    lastTrack = 1;
    size_t startByte = 0;
    size_t endByte = 0;          // exclusive; equals startByte for a caret
    /** Total bytes the effect will actually touch, per-track clamping included. */
    size_t bytes = 0;
};

class EffectRunner
{
public:
    explicit EffectRunner (Engine& e) : engine (e) {}

    /** Can `module` run on `windowId` as things stand? */
    Applicability check (uint64_t windowId, const EffectModule& module) const;

    /** What applying to `windowId` would cover. Meaningless if check() is not Ok. */
    Scope scope (uint64_t windowId) const;

    struct Outcome
    {
        bool        ran = false;
        ModResult   result = kModNoError;
        /** The module's own STR# 1000 voice, when it has one (§7.7). */
        std::string message;
        /** Windows created because the effect rendered to a "New Document". */
        std::vector<uint64_t> spawnedWindows;
    };

    /**
     * Apply the effect. Destructive and immediate: there is no undo and there must
     * never be one (§1).
     *
     * `answerExtend` answers the module's AskDoExtend() — Echo and Delay ask
     * whether they may grow the buffer, and "Ask Each Time" is one of the app's
     * non-negotiables (§12).
     */
    Outcome apply (uint64_t windowId, const EffectModule& module, const Settings& prefs,
                   bool answerExtend);

private:
    Engine& engine;
};

} // namespace se
