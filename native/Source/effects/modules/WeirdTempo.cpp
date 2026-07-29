// Weird Tempo — SPEC §8 #36. [BINARY: disassembly/Weird-Tempo.md]
// Ported from `src/effects/weird-tempo.ts`.
//
// A zero-crossing-aligned PERIOD DOUBLER: chop the selection into pseudo-periods
// at falling zero-crossings and write each one twice. No windowing, no
// interpolation, no overlap-add — the most naive possible time-stretch. Exact for
// periodic signals; on real audio the pseudo-periods are irregular, and that
// judder is the "really weird" the name promises.
//
// Three things that look like bugs and are not:
//   - Silence and unipolar material pass through UNCHANGED — no falling crossing
//     ever fires, so nothing is ever doubled.
//   - doExtend is ignored entirely. Weird Tempo always lengthens the sound,
//     whatever the host preference says. (Hence no Requirements::mayExtend: the
//     panel's toggle would be a lie here, since refusing changes nothing.)
//   - It warns on EVERY single run, and the warning apologises for itself.

#include "Modules.h"

#include "../Skeleton.h"
#include "engine/SampleFormat.h"

#include <vector>

namespace se::modules {

namespace {

/**
 * The segmentation state machine, lifted straight from the disassembly.
 *
 * Two booleans, `wantPos` and `phase`, both start true. A HIT fires when the
 * sample's sign matches what is sought (wantPos ⇒ sample ≥ 0, else sample < 0);
 * each hit toggles both. Every SECOND hit ends a segment — i.e. each sample where
 * the signal goes negative after having been non-negative.
 *
 * Zero counts as positive. The first segment is therefore short: it runs from the
 * selection start to the first strictly negative sample after a non-negative one,
 * which is a half-period. Full periods follow.
 *
 * @returns segment end offsets (exclusive), in bytes.
 */
std::vector<size_t> segmentBoundaries (const uint8_t* data, size_t start, size_t end, int bps)
{
    const size_t frameSize = static_cast<size_t> (frameSizeFor (bps));
    std::vector<size_t> bounds;

    bool wantPos = true;
    bool phase = true;

    for (size_t p = start; p < end; p += frameSize)
    {
        const bool isPos = getSample (data, p, bps) >= 0;   // zero counts as positive
        const bool hit = wantPos ? isPos : ! isPos;
        if (! hit) continue;

        wantPos = ! wantPos;
        phase = ! phase;
        if (phase) bounds.push_back (p);                    // every second hit
    }

    return bounds;
}

} // namespace

EffectModule makeWeirdTempo()
{
    EffectModule m;
    m.name = "Weird Tempo";
    m.signature = "WeirdTempo";
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Doing weird things to the tempo…";
    m.strings = {
        "",
        "",
        "Warning: this module is a short hack which may introduce noise. You may try "
        "do a Smooth after this to eliminate some noise.",
    };

    m.effect = [] (SoundDocument& doc, Host& host, const Settings&) -> ModResult
    {
        // On EVERY run, before doing anything: error string #3 with a Cancel
        // button. Not a first-run nag — every time. The host collects it and the
        // UI layer decides how to voice it (§7.7); there is no confirmation hook
        // on this side, so the run proceeds.
        host.showError (3, true);

        const size_t frameSize = static_cast<size_t> (doc.frameSize());
        const int bps = doc.bps;

        host.setupProgress ("Doing weird things to the tempo…");
        expandEmptySelection (doc);

        const size_t total = host.getBytesToProcess (doc) / frameSize;
        // Unusually, canCancel is TRUE here — most modules hard-code false.
        Progress progress (host, total, true);

        ModResult result = kModCancel;
        size_t maxSelEnd = 0;
        bool   cancelled = false;

        for (const int index : doc.selectedTrackIndices())
        {
            if (cancelled) break;   // remaining tracks skipped, per the doc

            auto& track = doc.tracks[static_cast<size_t> (index)];
            const ByteRange r = trackRange (doc, index, doc.selSt, doc.selEnd);
            if (r.start == r.end) continue;

            // The pristine source; the original works in place against a copy it
            // has already made.
            const std::vector<uint8_t> src = track.data;
            const auto at = [&src] (size_t offset)
            {
                return src.begin() + static_cast<std::ptrdiff_t> (offset);
            };

            const std::vector<size_t> bounds = segmentBoundaries (src.data(), r.start, r.end, bps);

            // Build the output: everything before the selection, then each complete
            // segment twice, then the trailing partial once.
            std::vector<uint8_t> out;
            out.reserve (src.size() + (r.end - r.start));
            out.insert (out.end(), src.begin(), at (r.start));

            size_t segStart = r.start;
            for (const size_t b : bounds)
            {
                out.insert (out.end(), at (segStart), at (b));
                out.insert (out.end(), at (segStart), at (b));   // the segment twice
                segStart = b;

                if (progress.tick())
                {
                    cancelled = true;
                    break;
                }
            }

            // Trailing partial segment (no falling crossing after it): written once.
            out.insert (out.end(), at (segStart), at (r.end));
            out.insert (out.end(), at (r.end), src.end());

            const size_t selEnd = out.size() - (src.size() - r.end);
            track.data = std::move (out);

            if (selEnd > maxSelEnd) maxSelEnd = selEnd;
            result = kModNoError;
        }

        host.closeProgress();
        if (maxSelEnd > doc.selEnd) doc.selEnd = maxSelEnd;

        // Cancelling still finalises the track and returns noErr: the half-processed
        // result is kept AND made structurally consistent. The destructive
        // philosophy, engineered rather than merely tolerated.
        return result;
    };

    return m;
}

} // namespace se::modules
