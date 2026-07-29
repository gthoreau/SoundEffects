#include "EffectRunner.h"

#include "Selection.h"
#include "Skeleton.h"
#include "engine/Edit.h"

#include <algorithm>

namespace se {

const char* applicabilityText (Applicability a, const Requirements& reqs)
{
    switch (a)
    {
        case Applicability::Ok:              return "";
        case Applicability::NoWindow:        return "Select a sound window first.";
        case Applicability::NoAudio:         return "This window has no audio yet.";
        // "At least" when it is a floor, "exactly" when it is a quota — the
        // sentence has to match which one the module actually asked for, or it
        // sends you off to change a selection that was already fine.
        case Applicability::NeedsTwoTracks:
            return reqs.exactSelectedTracks == 2 ? "Select exactly two tracks."
                                                 : "Select at least two tracks.";
        case Applicability::NeedsExactTracks:
            // Written out rather than templated: the only values this takes are
            // small, and a sentence beats "requires N track(s)".
            return reqs.exactSelectedTracks == 1 ? "Select exactly one track."
                                                 : "Select the required tracks.";
    }
    return "";
}

Applicability EffectRunner::check (uint64_t windowId, const EffectModule& module) const
{
    if (windowId == 0) return Applicability::NoWindow;

    auto* window = const_cast<Engine&> (engine).windowById (windowId);
    if (window == nullptr || window->uiDoc == nullptr) return Applicability::NoWindow;

    const SoundDocument& doc = *window->uiDoc;
    if (doc.maxTrackSize() == 0) return Applicability::NoAudio;

    const int selected = doc.lastSelTrack - doc.firstSelTrack + 1;

    const int required = module.requires.exactSelectedTracks;
    if (required > 0 && selected != required)
        return required == 2 ? Applicability::NeedsTwoTracks
                             : Applicability::NeedsExactTracks;

    const int minimum = module.requires.minSelectedTracks;
    if (minimum > 0 && selected < minimum)
        return minimum == 2 ? Applicability::NeedsTwoTracks
                            : Applicability::NeedsExactTracks;

    return Applicability::Ok;
}

Scope EffectRunner::scope (uint64_t windowId) const
{
    Scope s;

    auto* window = const_cast<Engine&> (engine).windowById (windowId);
    if (window == nullptr || window->uiDoc == nullptr) return s;

    const SoundDocument& doc = *window->uiDoc;

    s.wholeWindow = (doc.selSt == doc.selEnd);

    if (s.wholeWindow)
    {
        // "All the audio in the Window" — every track, full length. A caret leaves
        // the track range wherever the last drag put it, so this is the one place
        // the panel's rule adds to the reference's, which would keep the stale
        // track range and quietly skip the others.
        s.firstTrack = 1;
        s.lastTrack = doc.numTracks();
        s.startByte = 0;
        s.endByte = doc.maxTrackSize();
    }
    else
    {
        s.firstTrack = doc.firstSelTrack;
        s.lastTrack = doc.lastSelTrack;
        s.startByte = doc.selSt;
        s.endByte = doc.selEnd;
    }

    // Per-track clamping included: a track shorter than the selection contributes
    // only what it has, which is what the effect will really touch.
    for (int t = s.firstTrack - 1; t <= s.lastTrack - 1; ++t)
    {
        if (t < 0 || t >= doc.numTracks()) continue;
        const ByteRange r = trackRange (doc, t, s.startByte, s.endByte);
        if (r.end > r.start) s.bytes += r.end - r.start;
    }

    return s;
}

EffectRunner::Outcome EffectRunner::apply (uint64_t windowId, const EffectModule& module,
                                           const Settings& prefs, bool answerExtend)
{
    Outcome out;

    if (check (windowId, module) != Applicability::Ok) return out;

    SoundDocument* doc = engine.editDoc (windowId);
    if (doc == nullptr) return out;

    // Widen the track range to the whole Window when there is no time selection,
    // BEFORE the module runs — the skeleton expands an empty time range on its own
    // but never touches the track range. See scope().
    if (doc->selSt == doc->selEnd)
    {
        doc->firstSelTrack = 1;
        doc->lastSelTrack = doc->numTracks();
    }

    HostOptions opts;
    opts.askDoExtend = [answerExtend] { return answerExtend; };
    // Coarse: the progress window is not built yet, and polling every sample would
    // cost more than it reports. The modules that matter here hard-code
    // canCancel = false anyway (§7.6).
    opts.progressInterval = 4096;

    Host host (std::move (opts));
    host.doExtend = DoExtend::Ask;

    out.result = runEffect (module, *doc, host, prefs);
    out.ran = true;
    out.message = errorVoice (out.result, module.strings);

    // Anything the module said while running, in its own voice. A module can speak
    // without failing — Weird Tempo warns on every run and still returns noErr.
    for (const auto& reported : host.errors)
    {
        std::string voice = errorVoice (reported.errNum, module.strings);
        if (! voice.empty()) out.notices.push_back (std::move (voice));
    }

    // Publish to the audio thread. Safe mid-playback: the swap is a pointer
    // exchange and the old document is retired to be freed on this thread.
    engine.commitEdit (windowId);

    // A module that rendered to a "New Document" hands it back through the host,
    // because effect() can only return an OSErr. Those become real Windows.
    for (auto& spawned : host.spawnedDocuments)
    {
        const uint64_t id = engine.addWindow (std::move (spawned));
        if (id != 0) out.spawnedWindows.push_back (id);
    }

    return out;
}

} // namespace se
