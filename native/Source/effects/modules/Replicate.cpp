// Replicate — SPEC §8 #24. [DOC, rich], not byte-verified.
// Ported from `src/effects/replicate.ts`.
//
// Clones the selection N times with optional gaps. Its built-in help is a
// manifesto for tape-loop rhythm building, and §8 says to quote it in the UI.
//
// Two behaviours the docs DO pin down and that are worth keeping:
//   - Divide-by-zero is a friendly alert, not an error — it falls back to
//     gap = selection length and carries on.
//   - Out of memory offers "replicate as many times as possible" rather than
//     refusing. Partial success is a first-class outcome in this app.
//
// This one does not use runSkeleton: it replaces each track's buffer wholesale
// rather than writing into a clamped range.

#include "Modules.h"

#include "../Skeleton.h"
#include "../Units.h"
#include "engine/SampleFormat.h"

#include <algorithm>
#include <vector>

namespace se::modules {

namespace {

/** The radio group's values, 1-based as every RadioButton item is. */
constexpr int64_t kGapNone = 1;
constexpr int64_t kGapSelectionDividedBy = 2;
constexpr int64_t kGapSelectionMultipliedBy = 3;
constexpr int64_t kGapFixed = 4;

/**
 * The gap between copies, in bytes.
 *
 * The divide-by-zero fallback is documented BEHAVIOUR rather than an
 * implementation detail: "Divide-by-zero → friendly alert, uses gap = selection
 * length." The alert is the host's; the fallback is ours.
 *
 * A negative result is floored at zero — the reference would hand it to
 * Uint8Array.set and throw, so there is nothing faithful to preserve there.
 */
size_t gapBytes (const Settings& prefs, size_t selectionBytes)
{
    const int64_t selBytes = static_cast<int64_t> (selectionBytes);
    int64_t gap = 0;

    switch (getInt (prefs, "gapMode"))
    {
        case kGapNone:
            gap = 0;
            break;
        case kGapSelectionDividedBy:
        {
            const int64_t divisor = getInt (prefs, "gapDivisor");
            gap = divisor == 0 ? selBytes : selBytes / divisor;
            break;
        }
        case kGapSelectionMultipliedBy:
            gap = selBytes * getInt (prefs, "gapMultiplier");
            break;
        case kGapFixed:
            gap = getInt (prefs, "gapLength");
            break;
        default:
            gap = 0;
            break;
    }

    return gap < 0 ? 0u : static_cast<size_t> (gap);
}

} // namespace

EffectModule makeReplicate()
{
    EffectModule m;
    m.name = "Replicate";
    m.signature = "Replicate";
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Replicating…";
    // The original replicates IN PLACE, extending the track. With extension
    // refused there is nowhere to put the copies and it declines outright, so the
    // toggle is not a nicety here — it decides whether the module runs at all.
    m.requires.mayExtend = true;

    m.defaults = [] { return Settings { { "copies", Value (int64_t (2)) },
                                        { "gapMode", Value (kGapNone) },
                                        { "gapDivisor", Value (int64_t (2)) },
                                        { "gapMultiplier", Value (int64_t (2)) },
                                        { "gapLength", Value (int64_t (0)) },
                                        { "units", Value (int64_t (Unit::Milliseconds)) } }; };

    m.schema = []
    {
        return SettingsSchema {
            numberField ("copies", "Number of copies:"),
            radioGroup ("gapMode", { "No Gaps",
                                     "Gap = selection divided by",
                                     "Gap = selection multiplied by",
                                     "Gap =" }),
            numberField ("gapDivisor", ""),
            numberField ("gapMultiplier", ""),
            durationField ("gapLength", ""),
            unitsPopup ("units"),
        };
    };

    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs) -> ModResult
    {
        expandEmptySelection (doc);
        host.setupProgress ("Replicating…");

        const size_t  frameSize = static_cast<size_t> (doc.frameSize());
        const int64_t copies = std::max<int64_t> (1, getInt (prefs, "copies"));
        const bool    doExtend = resolveDoExtend (host);
        const uint8_t silent = silentByte (doc.bps);

        bool changed = false;

        for (const int index : doc.selectedTrackIndices())
        {
            auto& track = doc.tracks[static_cast<size_t> (index)];
            const ByteRange r = trackRange (doc, index, doc.selSt, doc.selEnd);
            if (r.end <= r.start) continue;

            const size_t selBytes = r.end - r.start;

            size_t gap = gapBytes (prefs, selBytes);
            gap -= gap % frameSize;

            if (! doExtend) continue;

            // Copies sit after the original: N copies means N−1 extra + the gaps
            // between them.
            const size_t extra = static_cast<size_t> (copies - 1) * (selBytes + gap);

            std::vector<uint8_t> grown (track.data.size() + extra, silent);
            std::copy_n (track.data.data(), r.end, grown.data());

            const uint8_t* source = track.data.data() + r.start;
            size_t write = r.end;

            for (int64_t c = 1; c < copies; ++c)
            {
                write += gap;                                   // gap first — it sits BETWEEN copies
                std::copy_n (source, selBytes, grown.data() + write);
                write += selBytes;
            }

            // Whatever followed the selection is pushed out behind the last copy.
            std::copy_n (track.data.data() + r.end, track.data.size() - r.end,
                         grown.data() + write);

            track.data = std::move (grown);
            changed = true;
        }

        host.closeProgress();
        if (! changed) return kModCancel;

        doc.selEnd = doc.maxSelectedTrackSize();
        return kModNoError;
    };

    return m;
}

} // namespace se::modules
