// Granular Synthesis — SPEC §8 #13.
// [BINARY: disassembly/Granular-Synthesis.md] for the kernels;
// [INTERPRETED] for the grain scheduling.
// Ported from `src/effects/granular.ts`.
//
// Read this before trusting the output: the recovery covers the PIECES — the grain
// envelope (confirmed three ways: 68k build, 68881 build, and the Read Me's
// published equation recovered as Expressionist source from PICT 1001), the
// Curvature tween, and the ANSI-C rand() driving Variance. What it does NOT cover
// is how grains are SCHEDULED: the hop between them, how Density maps to overlap,
// how the source cursor advances, and whether overlapping grains add or replace.
// SPEC §8 has only "quasi-Gaussian grains stepped through the source".
//
// So: every formula in Norris.h is verified, and the loop that calls them here is a
// reasonable reading. See SPEC §14 item 21.
//
// Norris breaks the house style on purpose — the envelope is a FLOATING-POINT
// per-sample gain, then clamped, not Ricci's integer-only convention. That is why
// this module shipped FPU and PowerPC builds.

#include "Modules.h"
#include "Norris.h"

#include "../Rng.h"
#include "../Skeleton.h"
#include "../Units.h"
#include "engine/SampleFormat.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace se::modules {

EffectModule makeGranularSynthesis()
{
    EffectModule m;
    m.name = "Granular Synthesis";
    m.signature = "GranularSynthesis";
    m.group = EffectGroup::MikesModules;
    m.version = "1.0";
    m.author = "Michael Norris";
    m.progressText = "Granulating…";
    m.requires.createsTracks = true;

    m.defaults = [] { return Settings { { "newLength", Value (int64_t (222000)) },
                                        { "units", Value (int64_t (Unit::Milliseconds)) },
                                        { "initialGrainLength", Value (int64_t (1000)) },
                                        { "finalGrainLength", Value (int64_t (1000)) },
                                        { "variance", Value (int64_t (0)) },
                                        { "curvature", Value (int64_t (100)) },
                                        { "initialDensity", Value (int64_t (100)) },
                                        { "finalDensity", Value (int64_t (100)) },
                                        { "densityCurvature", Value (int64_t (100)) },
                                        { "fadeLevel", Value (int64_t (0)) } }; };

    m.schema = []
    {
        return SettingsSchema {
            durationField ("newLength", "New Length:"),
            unitsPopup ("units"),
            durationField ("initialGrainLength", "Initial Grain Length:"),
            durationField ("finalGrainLength", "Final Grain Length:"),
            durationField ("variance", "Variance:"),
            numberField ("curvature", "Curvature:"),
            numberField ("initialDensity", "Initial Density:"),
            numberField ("finalDensity", "Final Density:"),
            numberField ("densityCurvature", "Density Curvature:"),
            numberField ("fadeLevel", "Fade Level:"),
        };
    };

    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs) -> ModResult
    {
        expandEmptySelection (doc);
        host.setupProgress ("Granulating…");

        const size_t frameSize = static_cast<size_t> (doc.frameSize());
        const Host::Limits limits = Host::getSampleValueLimits (doc.bps);
        const int bps = doc.bps;

        // The Norris LCG, NOT the Toolbox trap the Ricci modules use. Collapsing
        // the two would change what half the catalogue sounds like.
        AnsiCRandom rng (static_cast<uint32_t> (getInt (prefs, "seed", 1)));

        const double f = static_cast<double> (getInt (prefs, "fadeLevel")) / 100.0;
        const double initialGrain = static_cast<double> (getInt (prefs, "initialGrainLength"));
        const double finalGrain = static_cast<double> (getInt (prefs, "finalGrainLength"));
        const double variance = static_cast<double> (getInt (prefs, "variance"));
        const double curvature = static_cast<double> (getInt (prefs, "curvature"));
        const double initialDensity = static_cast<double> (getInt (prefs, "initialDensity"));
        const double finalDensity = static_cast<double> (getInt (prefs, "finalDensity"));
        const double densityCurvature = static_cast<double> (getInt (prefs, "densityCurvature"));

        const int srcIdx = doc.firstSelTrack - 1;
        if (srcIdx < 0 || srcIdx >= doc.numTracks())
        {
            host.closeProgress();
            return kModCancel;
        }

        const std::vector<uint8_t>& src = doc.tracks[static_cast<size_t> (srcIdx)].data;
        const ByteRange range = trackRange (doc, srcIdx, doc.selSt, doc.selEnd);
        const int64_t srcFrames = static_cast<int64_t> ((range.end - range.start) / frameSize);
        if (srcFrames < 1)
        {
            host.closeProgress();
            return kModCancel;
        }

        const int64_t outFrames = static_cast<int64_t> (getInt (prefs, "newLength"))
                                / static_cast<int64_t> (frameSize);
        if (outFrames < 1)
        {
            host.closeProgress();
            return kModCancel;
        }

        // Accumulate in floats: grains overlap, and the envelope is a float gain.
        // [INTERPRETED]: that overlapping grains ADD is the standard granular
        // reading and the only one under which Density > 100% means anything, but
        // the recovery shows only a per-grain "clamp and write back" and does not
        // settle add-vs-replace.
        std::vector<double> acc (static_cast<size_t> (outFrames), 0.0);

        int64_t outPos = 0;
        int64_t guard = 0;
        while (outPos < outFrames && guard++ < outFrames * 4)
        {
            // Grain length: the tween, jittered by Variance, truncated before use
            // as a byte count (FTINTX — the truncate-don't-round habit).
            double grainBytes = tween (static_cast<double> (outPos), static_cast<double> (outFrames),
                                       initialGrain, finalGrain, curvature);
            if (variance > 0.0)
            {
                // rand() is 0..0x7FFF; centre it to ±variance.
                const double jitter =
                    (static_cast<double> (rng.next()) / static_cast<double> (AnsiCRandom::kRandMax)) * 2.0 - 1.0;
                grainBytes += jitter * variance;
            }

            const int64_t grainFrames =
                std::max<int64_t> (1, static_cast<int64_t> (std::trunc (grainBytes / static_cast<double> (frameSize))));

            // The source cursor steps through the selection proportionally, so the
            // output traverses the source once across its whole length.
            const int64_t srcStart = static_cast<int64_t> (
                std::trunc ((static_cast<double> (outPos) / static_cast<double> (outFrames))
                            * static_cast<double> (srcFrames)));

            for (int64_t x = 0; x < grainFrames; ++x)
            {
                const int64_t o = outPos + x;
                if (o >= outFrames) break;
                const int64_t s = srcStart + x;
                if (s >= srcFrames) break;

                const int32_t sample = getSample (src.data(),
                                                  range.start + static_cast<size_t> (s) * frameSize, bps);
                // The verified kernel: a float gain from the grain envelope.
                acc[static_cast<size_t> (o)] += static_cast<double> (sample)
                    * grainEnvelope (static_cast<double> (x), static_cast<double> (grainFrames), f);
            }

            // Density -> hop. 100% is grains back-to-back; higher overlaps them.
            const double density = std::max (1.0,
                tween (static_cast<double> (outPos), static_cast<double> (outFrames),
                       initialDensity, finalDensity, densityCurvature));
            const int64_t hop = std::max<int64_t> (1,
                static_cast<int64_t> (std::trunc ((static_cast<double> (grainFrames) * 100.0) / density)));

            outPos += hop;
        }

        std::vector<uint8_t> out (static_cast<size_t> (outFrames) * frameSize, silentByte (bps));
        for (int64_t i = 0; i < outFrames; ++i)
        {
            int64_t v = static_cast<int64_t> (std::trunc (acc[static_cast<size_t> (i)]));
            if (v < limits.min) v = limits.min;
            else if (v > limits.max) v = limits.max;
            setSample (out.data(), static_cast<size_t> (i) * frameSize, static_cast<int32_t> (v), bps);
        }

        // Granulates INTO a new track.
        host.addNewTrack (doc);
        doc.tracks[static_cast<size_t> (doc.numTracks() - 1)].data = std::move (out);

        host.closeProgress();
        return kModNoError;
    };

    return m;
}

} // namespace se::modules
