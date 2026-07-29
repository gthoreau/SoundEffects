// Zero Crosser — SPEC §8 #37. [DOC+RES], not byte-verified.
// Ported from `src/effects/zero-crosser.ts`.
//
// Non-audio: it moves the SELECTION to the nearest zero-crossings and never writes
// a sample. Its group is "Selection", and it is the proof that the module ABI is
// general enough to host a selection tool — effect() may mutate selSt/selEnd and
// return, touching nothing else.
//
// [INTERPRETED]: "nearest zero-crossings" is all the docs give. We take a crossing
// to be a SIGN CHANGE BETWEEN ADJACENT FRAMES, with zero counting as positive —
// the convention Weird Tempo's segmenter provably uses, and the only other place
// in the app that hunts for crossings. Whether Expand/Shrink search a bounded
// window, and what happens when no crossing exists, are unrecorded; we leave the
// edge where it was.

#include "Modules.h"

#include "../Skeleton.h"
#include "engine/SampleFormat.h"

#include <vector>

namespace se::modules {

namespace {

/** action: 1 = Expand, 2 = Shrink — the radio group's only two values, so the
    module tests for Expand and treats everything else as Shrink. */
constexpr int64_t kActionExpand = 1;

constexpr int64_t kSideLeft = 1;
constexpr int64_t kSideRight = 2;
constexpr int64_t kSideBoth = 3;

/** Zero counts as positive — Weird Tempo's convention. */
bool isPositive (const std::vector<uint8_t>& data, int64_t p, int bps) noexcept
{
    return getSample (data.data(), static_cast<size_t> (p), bps) >= 0;
}

/**
 * Search from `from` in `step` direction for the first frame whose sign differs
 * from its predecessor. Returns false if the buffer (or `limit`) runs out first,
 * and then the caller leaves that edge alone.
 *
 * Signed throughout: a leftward search deliberately walks off the front of the
 * buffer, and that negative offset is the loop's exit condition.
 */
bool findCrossing (const std::vector<uint8_t>& data, int bps,
                   int64_t from, int64_t step, int64_t limit, int64_t& found)
{
    const int64_t frameSize = frameSizeFor (bps);
    const int64_t length = static_cast<int64_t> (data.size());

    int64_t p = from;
    if (p < 0 || p + frameSize > length) return false;

    bool sign = isPositive (data, p, bps);

    for (;;)
    {
        const int64_t next = p + step * frameSize;
        if (next < 0 || next + frameSize > length) return false;
        if (step > 0 ? next >= limit : next < limit) return false;

        const bool s = isPositive (data, next, bps);
        if (s != sign)
        {
            // Going right, the crossing frame itself is the new edge; going left,
            // selEnd is exclusive, so it sits one frame past it.
            found = step > 0 ? next : next + frameSize;
            return true;
        }

        sign = s;
        p = next;
    }
}

} // namespace

EffectModule makeZeroCrosser()
{
    EffectModule m;
    m.name = "Zero Crosser";
    m.signature = "ZeroCrosser";
    m.group = EffectGroup::Selection;
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "";

    m.defaults = [] { return Settings { { "action", Value (kActionExpand) },
                                        { "side", Value (kSideBoth) } }; };

    m.schema = []
    {
        return SettingsSchema {
            radioGroup ("action", { "Expand", "Shrink" }),
            staticText ("the selection to nearest zero-crossings on"),
            radioGroup ("side", { "Left", "Right", "Both" }),
            staticText ("side(s)"),
        };
    };

    // No progress window and no host services at all: the only module in the set
    // that never opens one, because there is nothing to report on.
    m.effect = [] (SoundDocument& doc, Host&, const Settings& prefs) -> ModResult
    {
        expandEmptySelection (doc);

        const int index = doc.firstSelTrack - 1;
        if (index < 0 || index >= doc.numTracks()) return kModCancel;

        const std::vector<uint8_t>& data = doc.tracks[static_cast<size_t> (index)].data;
        const int bps = doc.bps;

        const bool    expand = getInt (prefs, "action") == kActionExpand;
        const int64_t side = getInt (prefs, "side");
        const bool    doLeft = side == kSideLeft || side == kSideBoth;
        const bool    doRight = side == kSideRight || side == kSideBoth;

        const int64_t frameSize = doc.frameSize();

        int64_t selSt = static_cast<int64_t> (doc.selSt);
        int64_t selEnd = static_cast<int64_t> (doc.selEnd);
        int64_t found = 0;

        if (doLeft)
        {
            // selSt is inclusive, so the search starts on it. Expand hunts outward
            // (left), shrink inward (right).
            const bool hit = expand ? findCrossing (data, bps, selSt, -1, 0, found)
                                    : findCrossing (data, bps, selSt, +1, selEnd, found);
            if (hit) selSt = found;
        }

        if (doRight)
        {
            // selEnd is EXCLUSIVE — the last sample in the selection is one frame
            // back, and starting the scan on selEnd itself reads past the buffer.
            const int64_t last = selEnd - frameSize;
            const bool hit = expand
                ? findCrossing (data, bps, last, +1, static_cast<int64_t> (data.size()), found)
                : findCrossing (data, bps, last, -1, selSt, found);
            if (hit) selEnd = found;
        }

        if (selSt == static_cast<int64_t> (doc.selSt)
            && selEnd == static_cast<int64_t> (doc.selEnd)) return kModCancel;
        if (selEnd < selSt) return kModCancel;

        doc.selSt = static_cast<size_t> (selSt);
        doc.selEnd = static_cast<size_t> (selEnd);

        // Returns noError having touched no audio at all — the selection IS the
        // product.
        return kModNoError;
    };

    return m;
}

} // namespace se::modules
