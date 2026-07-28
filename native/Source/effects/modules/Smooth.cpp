// Smooth — SPEC §8 #32. [BINARY: disassembly/Smooth.md]
// Ported from `src/effects/smooth.ts`.
//
// The cautionary tale of this whole project. The About box says "averaging every
// sample with the preceding and the following one", which reads like a 3-point
// (prev+cur+next)/3. The binary says otherwise:
//
//     out[i] = (in[i−1] + in[i+1]) / 2
//
// a 2-POINT average that EXCLUDES THE CENTRE SAMPLE ENTIRELY. The current sample
// plays no part in its own replacement. Docs can lie; this is why the disassembly
// pass happened, and why every [DOC]-only module in this catalogue is suspect.

#include "Modules.h"

#include "../Skeleton.h"
#include "engine/SampleFormat.h"

namespace se::modules {

EffectModule makeSmooth()
{
    EffectModule m;
    m.name = "Smooth";
    m.signature = "Smooth";
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Smoothing…";
    m.strings = { "The Smooth effect does not have any options to set." };

    m.effect = [] (SoundDocument& doc, Host& host, const Settings&) -> ModResult
    {
        const size_t frameSize = static_cast<size_t> (doc.frameSize());
        const int bps = doc.bps;

        return runSkeleton (doc, host, { "Smoothing…", false },
                            [frameSize, bps] (const TrackPass& pass, Progress& progress) -> ModResult
        {
            // At least two frames, or the loop body never runs. (A real smooth
            // needs three: with exactly two, both samples are boundary samples.)
            if (pass.end < frameSize || pass.end - frameSize <= pass.start)
                return kModNoError;

            // Seed: the first sample's ORIGINAL value, read once before the loop.
            // It is never itself overwritten.
            int32_t prev = getSample (pass.data, pass.start, bps);

            // The write cursor starts at the second sample and stops before the
            // last — the two boundary samples are excluded from the loop
            // entirely, rather than edge-clamped or padded as most smoothers do.
            for (size_t p = pass.start + frameSize; p < pass.end - frameSize; p += frameSize)
            {
                const int32_t next = getSample (pass.data, p + frameSize, bps);

                // FLOOR, not truncate-toward-zero — see floorDiv. (-1 + 0)/2 is
                // -1 here, where C's signed / would give 0.
                const int64_t avg = floorDiv (static_cast<int64_t> (prev) + next, 2);

                // Capture this position's ORIGINAL value before overwriting it, so
                // the next iteration's `prev` is a pristine input sample.
                // Non-cascading: every average uses two original neighbours,
                // never an already-smoothed one.
                prev = getSample (pass.data, p, bps);

                setSample (pass.data, p, static_cast<int32_t> (avg), bps);

                if (progress.tick()) return kModNoError;
            }

            // No clamp: the average of two in-range samples is always in range.
            // Length and selection are never touched — a pure fixed-length
            // transform.
            return kModNoError;
        });
    };

    return m;
}

} // namespace se::modules
