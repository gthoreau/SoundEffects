// Dither — SPEC §8 #6. [BINARY: disassembly/Dither-IIR-Filters.md]
// Ported from `src/effects/dither.ts`.
//
//     out = clamp( x + (amplitude · Random()) / 32767 )
//
// Pure integer, no floating point. Uses the Toolbox `_Random` trap — the SAME RNG
// as Noise, NOT the ANSI-C LCG the Norris modules use — but it ADDS where Noise
// replaces.
//
// Its dialog is module-drawn (DITL 128), not the host schema: explicit OK/Cancel,
// the label "Add", two radios and two fields, then "of random noise". The schema
// below therefore describes the settings for a host that wants to generate a form,
// but it is a reconstruction of a hand-drawn dialog, not a [SOURCE] one.

#include "Modules.h"

#include "../Skeleton.h"

namespace se::modules {

/**
 * amplitude = (pct · 2^(bps−1)) / 100 — integer, truncating.
 *
 * This reproduces the About text's own examples exactly: 10% gives 12 points at
 * 8-bit and 3276 at 16-bit. Note 2^(bps−1) is 128/32768 — one MORE than the true
 * max sample (127/32767). The overshoot is real and the per-sample clamp absorbs
 * it; "fixing" it to 2^(bps−1)−1 makes the documented examples stop matching.
 */
static int64_t ditherAmplitude (const Settings& prefs, int bps)
{
    constexpr int64_t kPoints = 2;
    if (getInt (prefs, "mode") == kPoints) return getInt (prefs, "points");

    const int64_t fullScale = int64_t (1) << (bps - 1);
    return (getInt (prefs, "percent") * fullScale) / 100;
}

EffectModule makeDither()
{
    EffectModule m;
    m.name = "Dither";
    m.signature = "Dither";
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Dithering…";

    // mode: 1 = percent of max amplitude, 2 = absolute points. 1-based, as radios
    // are throughout ModSetups.h.
    m.defaults = [] { return Settings { { "mode", Value (int64_t (1)) },
                                        { "percent", Value (int64_t (10)) },
                                        { "points", Value (int64_t (12)) } }; };

    m.schema = []
    {
        return SettingsSchema {
            staticText ("Add"),
            radioGroup ("mode", { "%", "points" }),
            numberField ("percent", "%"),
            numberField ("points", "points"),
            staticText ("of random noise"),
        };
    };

    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs) -> ModResult
    {
        const int64_t amp = ditherAmplitude (prefs, doc.bps);

        return perSampleEffect (doc, host, { "Dithering…", false },
                                [amp, &host] (int32_t s, const TrackPass&, size_t) -> int64_t
                                {
                                    const int64_t r = host.random.next();   // signed word
                                    return static_cast<int64_t> (s) + (amp * r) / 32767;
                                });
    };

    return m;
}

} // namespace se::modules
