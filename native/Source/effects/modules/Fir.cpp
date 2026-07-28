// The four "FIR" filters — SPEC §8 #2/#3/#14/#19. [DOC], not byte-verified.
// Ported from `src/effects/fir.ts`.
//
//     Low Pass FIR   y(n) = [x(n) + x(n−1)] / 2
//     High Pass FIR  y(n) = [x(n) − x(n−1)] / 2
//     Bandpass FIR   y(n) = x(n) − x(n−2) − 0.25·y(n−2)
//     Bandstop FIR   y(n) = [x(n) + x(n−2) + 0.25·y(n−2)] / 2
//
// Two caveats worth carrying:
//
//   1. Bandpass and Bandstop are MISNOMERS — both have a recursive y(n−2) term, so
//      they are not FIR at all. The names ship as-is (§8 #2).
//   2. These are [DOC] formulas. The three IIR filters were also "published
//      one-liners" until the disassembly found 0.1 coefficient quantisation,
//      per-term truncation and first-sample seeding hiding underneath. The same
//      unwritten details almost certainly apply here — in particular the 0.25 is
//      very unlikely to be floating-point in a module that ships no FPU build.
//      Treat the edge behaviour as a guess; see SPEC §14.

#include "Modules.h"

#include "../Skeleton.h"
#include "engine/SampleFormat.h"

namespace se::modules {

namespace {

const char* kNoOptions = "Sorry, man. I haven't got any options.";

/**
 * One-tap filters. [DOC]
 *
 * We follow the house style the disassembly established everywhere else: seed the
 * delay from the first sample, leave that first sample untouched, and FLOOR the
 * halving (as Smooth provably does). None of that is verified for these modules —
 * it is the most probable reading given every sibling behaves so.
 */
ModResult oneTap (SoundDocument& doc, Host& host, int sign, const char* text)
{
    const size_t frameSize = static_cast<size_t> (doc.frameSize());
    const Host::Limits limits = Host::getSampleValueLimits (doc.bps);
    const int bps = doc.bps;

    return runSkeleton (doc, host, { text, false },
                        [=] (const TrackPass& pass, Progress& progress) -> ModResult
    {
        int32_t xprev = getSample (pass.data, pass.start, bps);

        for (size_t p = pass.start + frameSize; p < pass.end; p += frameSize)
        {
            const int32_t x = getSample (pass.data, p, bps);

            int64_t y = floorDiv (static_cast<int64_t> (x) + sign * static_cast<int64_t> (xprev), 2);
            if (y < limits.min) y = limits.min;
            else if (y > limits.max) y = limits.max;

            setSample (pass.data, p, static_cast<int32_t> (y), bps);
            xprev = x;

            if (progress.tick()) return kModNoError;
        }
        return kModNoError;
    });
}

/**
 * Two-tap recursive filters. [DOC]
 *
 * The 0.25·y(n−2) is expressed as /4, integer and TRUNCATING — the house
 * convention, and the only reading consistent with these modules shipping no FPU
 * build. Note the mixed rounding: the inner /4 truncates toward zero while
 * bandstop's outer halving floors. Unverified, and preserved as the reference has
 * it rather than made consistent.
 */
ModResult twoTap (SoundDocument& doc, Host& host, bool bandpass, const char* text)
{
    const size_t frameSize = static_cast<size_t> (doc.frameSize());
    const Host::Limits limits = Host::getSampleValueLimits (doc.bps);
    const int bps = doc.bps;

    return runSkeleton (doc, host, { text, false },
                        [=] (const TrackPass& pass, Progress& progress) -> ModResult
    {
        const int32_t first = getSample (pass.data, pass.start, bps);
        int64_t x2 = first, x1 = first, y2 = first, y1 = first;

        for (size_t p = pass.start + frameSize; p < pass.end; p += frameSize)
        {
            const int64_t x = getSample (pass.data, p, bps);

            int64_t y = bandpass ? (x - x2 - (y2 / 4))
                                 : floorDiv (x + x2 + (y2 / 4), 2);

            if (y < limits.min) y = limits.min;
            else if (y > limits.max) y = limits.max;

            setSample (pass.data, p, static_cast<int32_t> (y), bps);

            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;

            if (progress.tick()) return kModNoError;
        }
        return kModNoError;
    });
}

EffectModule firModule (std::string name, std::string signature, std::string comment)
{
    EffectModule m;
    m.name = std::move (name);
    m.signature = std::move (signature);
    m.group = EffectGroup::FirIirFilters;
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Filtering…";
    m.strings = { kNoOptions };
    m.about = std::move (comment);
    return m;
}

} // namespace

EffectModule makeLowPassFIR()
{
    EffectModule m = firModule ("Low Pass FIR", "LowPassFIR", {});
    m.effect = [] (SoundDocument& doc, Host& host, const Settings&)
    {
        return oneTap (doc, host, 1, "Filtering…");
    };
    return m;
}

EffectModule makeHighPassFIR()
{
    EffectModule m = firModule ("High Pass FIR", "HighPassFIR", {});
    m.effect = [] (SoundDocument& doc, Host& host, const Settings&)
    {
        return oneTap (doc, host, -1, "Filtering…");
    };
    return m;
}

EffectModule makeBandpassFIR()
{
    // Named "FIR" but has a recursive term. Zeros at z=±1, poles at ±0.5i.
    EffectModule m = firModule ("Bandpass FIR", "BandpassFIR", {});
    m.effect = [] (SoundDocument& doc, Host& host, const Settings&)
    {
        return twoTap (doc, host, true, "Filtering…");
    };
    return m;
}

EffectModule makeBandstopFIR()
{
    // Zeros ±i, poles ±0.5.
    EffectModule m = firModule ("Bandstop FIR", "BandstopFIR", {});
    m.effect = [] (SoundDocument& doc, Host& host, const Settings&)
    {
        return twoTap (doc, host, false, "Filtering…");
    };
    return m;
}

} // namespace se::modules
