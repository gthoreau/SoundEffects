// Iterative Crossfade — SPEC §8 #17.
// [BINARY: disassembly/Sample-Hose-Iterative-Crossfade.md]
// Ported from `src/effects/iterative-crossfade.ts`.
//
//     r   = pos / len              sweeping 0 → 1
//     out = clamp(trunc( A·(1−r) + B·r ))
//
// A plain linear crossfade in double precision, truncated then clamped. It shares
// NOTHING with its siblings — no RNG, no transcendentals, no tween — and ships no
// FPU build, because add/sub/mul/div is all it needs.
//
// Tracks that have run out contribute DIGITAL ZERO. It zero-pads rather than
// skipping, so a short sound fades into or out of silence. That is the opposite of
// Select Peak's skip-the-short-track rule, and conflating them would be easy.

#include "Modules.h"

#include "../Skeleton.h"
#include "../Units.h"
#include "engine/SampleFormat.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace se::modules {

EffectModule makeIterativeCrossfade()
{
    EffectModule m;
    m.name = "Iterative Crossfade";
    m.signature = "IterativeCrossfade";
    m.group = EffectGroup::MikesModules;
    m.version = "1.0.2";
    m.author = "Michael Norris";
    m.progressText = "Crossfading…";
    m.about = "To run this module you must first have two channels selected. This module "
              "then creates a new channel, which contains iterations of a gradual "
              "crossfade between the two selected samples.";
    m.requires.minSelectedTracks = 2;
    m.requires.createsTracks = true;

    m.defaults = [] { return Settings { { "iterations", Value (int64_t (10)) },
                                        { "silenceLength", Value (int64_t (0)) },
                                        { "units", Value (int64_t (Unit::Milliseconds)) } }; };

    m.schema = []
    {
        return SettingsSchema {
            numberField ("iterations", "Number of Iterations:"),
            durationField ("silenceLength", "Silence Length:"),
            unitsPopup ("units"),
        };
    };

    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs) -> ModResult
    {
        if (doc.lastSelTrack - doc.firstSelTrack + 1 < 2) return kModCancel;

        expandEmptySelection (doc);
        host.setupProgress ("Crossfading…");

        const size_t frameSize = static_cast<size_t> (doc.frameSize());
        const Host::Limits limits = Host::getSampleValueLimits (doc.bps);
        const int bps = doc.bps;

        const int aIdx = doc.firstSelTrack - 1;
        const int bIdx = doc.firstSelTrack;
        if (aIdx < 0 || bIdx >= doc.numTracks())
        {
            host.closeProgress();
            return kModCancel;
        }

        const std::vector<uint8_t>& a = doc.tracks[static_cast<size_t> (aIdx)].data;
        const std::vector<uint8_t>& b = doc.tracks[static_cast<size_t> (bIdx)].data;
        const ByteRange ra = trackRange (doc, aIdx, doc.selSt, doc.selEnd);
        const ByteRange rb = trackRange (doc, bIdx, doc.selSt, doc.selEnd);

        // l = the selection length, in frames.
        const size_t l = std::max ((ra.end - ra.start) / frameSize,
                                   (rb.end - rb.start) / frameSize);
        if (l < 1)
        {
            host.closeProgress();
            return kModCancel;
        }

        const int64_t n = std::max<int64_t> (1, getInt (prefs, "iterations"));
        // Silence, in frames. Zero silence on short samples is what gives the
        // quasi-granular behaviour.
        const size_t s = static_cast<size_t> (std::max<int64_t> (0, getInt (prefs, "silenceLength")))
                       / frameSize;

        // New length = n·(l+s) − s: silence sits BETWEEN iterations, not after the
        // last one.
        const size_t outFrames = static_cast<size_t> (n) * (l + s) - s;
        std::vector<uint8_t> out (outFrames * frameSize, silentByte (bps));

        for (int64_t iter = 0; iter < n; ++iter)
        {
            const size_t base = static_cast<size_t> (iter) * (l + s);

            for (size_t i = 0; i < l; ++i)
            {
                // [INTERPRETED]: r sweeps 0→1 across EACH iteration, per the
                // recovery's "r sweeps 0→1 across each fade" and the About text's
                // "iterations of a gradual crossfade". The alternative reading —
                // one crossfade spread across all n iterations — is defensible
                // from "renders n iterations crossfading from A to B" and would
                // make each iteration a different static mix. See SPEC §14.
                const double r = static_cast<double> (i) / static_cast<double> (l);

                const size_t pa = ra.start + i * frameSize;
                const size_t pb = rb.start + i * frameSize;

                // Zero-pad, don't skip.
                const double A = pa + frameSize <= ra.end
                    ? static_cast<double> (getSample (a.data(), pa, bps)) : 0.0;
                const double B = pb + frameSize <= rb.end
                    ? static_cast<double> (getSample (b.data(), pb, bps)) : 0.0;

                // Double precision, then truncate, then clamp — in that order.
                int64_t v = static_cast<int64_t> (std::trunc (A * (1.0 - r) + B * r));
                if (v < limits.min) v = limits.min;
                else if (v > limits.max) v = limits.max;

                setSample (out.data(), (base + i) * frameSize, static_cast<int32_t> (v), bps);
            }
        }

        // Renders into a NEW track.
        host.addNewTrack (doc);
        doc.tracks[static_cast<size_t> (doc.numTracks() - 1)].data = std::move (out);

        host.closeProgress();
        return kModNoError;
    };

    return m;
}

} // namespace se::modules
