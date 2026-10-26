// AudioSourcePopup — "Select Audio Source" (Figma 543:9503, the three variants
// Source=Canvas / External / Daw), on the Os8Popup frame.
//
// Rows, as drawn: [ ] Monitor  Gain: [volume slider] [100] %  — then "Select Audio
// source:" and a dropdown pair (the kind: External / Canvas / DAW, then the choice
// within it) — External adds an "Audio/Midi Settings" button under the pair — then
// Cancel / Apply. DAW is offered only where there is a host (the plug-in).
//
// Not in the design, added at the author's decision (PLAN.md, "Recording conversion"):
// a "Clean conversion" checkbox, Raw by default. It sits under the source rows.
//
// The popup edits a COPY of the Window's SourceChoice; Apply hands it back. Changing
// the kind rebuilds the popup (it has a different shape per kind), which is what
// `reopen` is for: it receives the choice as it stood.

#pragma once

#include "Os8Popup.h"
#include "engine/Engine.h"

#include <functional>
#include <memory>
#include <vector>

namespace audiosource {

/** What a Window records from, and how: kept per Window by the editor. */
struct SourceChoice
{
    se::AudioSourceKind kind = se::AudioSourceKind::external;
    int externalIndex = 0;        // into externalSources()
    uint64_t canvasWindow = 0;    // Canvas: 0 = the main mix, otherwise that Window's own output
    bool monitor = false;         // OFF by default, against feedback
    double gainPercent = 100.0;   // 0..200, 100 = unity
    bool clean = false;           // Raw (false) or Clean conversion
};

/** One entry of the External list: a mono input channel, or a stereo pair. */
struct ExternalSource
{
    juce::String name;
    int firstChannel = 0;
    bool stereo = false;
};

/** The device's inputs as the popup lists them: the pairs first ("Input 1 + 2"), then
    each channel alone. One input gives just "Input 1". */
std::vector<ExternalSource> externalSources (int numInputChannels);

struct Context
{
    se::Engine* engine = nullptr;
    uint64_t window = 0;                 // the Window being recorded into
    int numInputChannels = 0;
    bool dawAvailable = false;           // a plug-in under a host
    std::function<void()> openAudioSettings;   // standalone only; the button is hidden when null
};

struct Built
{
    std::unique_ptr<Os8Popup> popup;
};

/** Build the popup for `current`. `apply` gets the edited choice on Apply; `reopen`
    is called (after the popup has gone) when the kind changed and the popup must be
    shown again in its new shape. */
Built build (const Context&, const SourceChoice& current,
             std::function<void (const SourceChoice&)> apply,
             std::function<void (const SourceChoice&)> reopen);

/** The kinds the popup offers, in order (External, Canvas[, DAW]). */
std::vector<se::AudioSourceKind> offeredKinds (bool dawAvailable);

} // namespace audiosource
