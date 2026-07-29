// Echo and Delay — SPEC §8 #9/#4. [BINARY: disassembly/Echo-Delay.md]
// Ported from `src/effects/echo-delay.ts`.
//
// These are the SAME offset-and-scale kernel, and the sweep direction over the
// in-place buffer is the whole trick:
//
//     Echo  (forward):  dst[i+D] = clamp( dst[i+D] + trunc(src[i]·strength/100) )
//     Delay (backward): dst[i]   = clamp( dst[i]   + trunc(src[i−D]·strength/100) )
//
// Both are literally y[n] = clamp(y[n] + trunc(y[n−D]·strength/100)) — the same
// single-tap recurrence Robotize uses, with D and strength as parameters.
//
//   - Echo sweeps FORWARD, so a write at i+D is read again as a source once the
//     cursor reaches it. One pass is genuinely recursive and produces the entire
//     exponentially-decaying echo train by itself.
//   - Delay sweeps BACKWARD, so every read at i−D happens before that position is
//     ever a write target. Every read is pristine: exactly one copy, no repeats.
//
// The decay curve you hear is an emergent property of the recursion, not of any
// decay maths — echoMixLimit() below only sizes the buffer.

#include "Modules.h"

#include "../Skeleton.h"
#include "../Units.h"
#include "engine/SampleFormat.h"

#include <algorithm>

namespace se::modules {

namespace {

/** Recovered constants: the decay sizing loop stops at ≤8% or 80 echoes. */
constexpr int64_t kDecayAmpFloor = 8;
constexpr int     kMaxEchoes = 80;

/**
 * Echo's "until it decays" range, simulated BEFORE touching any samples — purely
 * to decide how far the buffer must grow. This loop never reads or writes audio.
 *
 * At strength 70% that is ceil(log(8/100)/log(0.7)) = 8 echoes; the 80-echo cap
 * only bites at strengths close to 100%.
 */
size_t echoMixLimit (size_t end, size_t delayBytes, int64_t strength) noexcept
{
    size_t  mixLimit = end;
    int64_t amp = 100;
    int     echoCount = 1;

    do
    {
        mixLimit += delayBytes;
        amp = (amp * strength) / 100;   // integer, truncating
        ++echoCount;
    } while (amp > kDecayAmpFloor && echoCount < kMaxEchoes);

    return mixLimit;
}

/**
 * The delay field is in BYTES whatever the units popup says (the host converts at
 * the dialog boundary, §2.3), and it is frame-aligned before use.
 *
 * The reference lets a negative value through — JS arithmetic just goes negative
 * and the degenerate guard catches it. Here it would wrap a size_t, so it is
 * floored at zero: same outcome for every value a field can sensibly hold.
 */
size_t alignedDelayBytes (int64_t raw, size_t frameSize) noexcept
{
    if (raw < 0) raw = 0;
    return static_cast<size_t> (raw) - static_cast<size_t> (raw) % frameSize;
}

SettingsSchema durationSchema (bool withOnlyCount)
{
    SettingsSchema s {
        durationField ("delay", "Delay:"),
        unitsPopup ("units"),
        numberField ("strength", "Strength:"),
        staticText ("%"),
    };

    if (withOnlyCount)
    {
        s.push_back (checkBox ("onlyOn", "Only"));
        s.push_back (numberField ("onlyCount", "echoes"));
    }

    return s;
}

} // namespace

// --- Echo --------------------------------------------------------------------

EffectModule makeEcho()
{
    EffectModule m;
    m.name = "Echo";
    m.signature = "Echo";
    m.group = EffectGroup::EchoesAndReverbs;
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Echoing…";
    // Refusing extension confines the decay to the selection's own overlap, which
    // is a real (quieter) result rather than a no-op — but it is a different sound,
    // so the panel must still ask.
    m.requires.mayExtend = true;

    m.defaults = [] { return Settings { { "delay", Value (int64_t (3500)) },
                                        { "strength", Value (int64_t (70)) },
                                        { "onlyOn", Value (false) },
                                        { "onlyCount", Value (int64_t (3)) },
                                        { "units", Value (int64_t (Unit::Milliseconds)) } }; };
    m.schema = [] { return durationSchema (true); };

    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs) -> ModResult
    {
        const size_t frameSize = static_cast<size_t> (doc.frameSize());
        const Host::Limits limits = Host::getSampleValueLimits (doc.bps);
        const int bps = doc.bps;

        const size_t  delayBytes = alignedDelayBytes (getInt (prefs, "delay"), frameSize);
        const int64_t strength = getInt (prefs, "strength");
        const bool    onlyOn = getBool (prefs, "onlyOn");
        const int64_t onlyCount = std::max<int64_t> (0, getInt (prefs, "onlyCount"));

        // Asked ONCE per run, before any track is touched — not per track.
        const bool doExtend = resolveDoExtend (host);

        size_t maxSelEnd = 0;

        const ModResult result = runSkeleton (doc, host, { "Echoing…", false },
                                              [&] (const TrackPass& pass, Progress& progress) -> ModResult
        {
            size_t mixLimit = onlyOn
                // "Only N echoes": a hard cap, no decay maths at all.
                ? pass.end + static_cast<size_t> (onlyCount) * delayBytes
                : echoMixLimit (pass.end, delayBytes, strength);

            // If extension is off, the decay is confined to the selection's own
            // overlap.
            if (! doExtend) mixLimit = pass.end;

            if (mixLimit > pass.size)
            {
                // Grows, silences the new tail, and degrades gracefully: if the
                // grow only partially succeeds the range silently shrinks, with no
                // error surfaced.
                mixLimit = std::min (mixLimit, growAndSilence (doc, pass.index, mixLimit));
            }

            // pass.data was captured before the grow, and growing REALLOCATES —
            // re-read the pointer, exactly as the reference re-reads ch.data.
            uint8_t* buf = doc.tracks[static_cast<size_t> (pass.index)].data.data();

            if (mixLimit <= pass.start + delayBytes) return kModNoError;   // degenerate
            if (mixLimit > maxSelEnd) maxSelEnd = mixLimit;

            // Forward: the destination of an early sample becomes a later source
            // read, and that is what builds the whole train in one pass.
            for (size_t src = pass.start; src < mixLimit - delayBytes; src += frameSize)
            {
                const int32_t here = getSample (buf, src, bps);
                const size_t  dst = src + delayBytes;

                int64_t ahead = getSample (buf, dst, bps);
                ahead += (static_cast<int64_t> (here) * strength) / 100;

                if (ahead < limits.min) ahead = limits.min;
                else if (ahead > limits.max) ahead = limits.max;

                setSample (buf, dst, static_cast<int32_t> (ahead), bps);

                if (progress.tick()) return kModNoError;
            }
            return kModNoError;
        });

        // selEnd extends to the max mixLimit across processed tracks — tracks drift
        // independently, same philosophy as Weird Tempo.
        if (maxSelEnd > doc.selEnd) doc.selEnd = maxSelEnd;
        return result;
    };

    return m;
}

// --- Delay -------------------------------------------------------------------

EffectModule makeDelay()
{
    EffectModule m;
    m.name = "Delay";
    m.signature = "Delay";
    m.group = EffectGroup::EchoesAndReverbs;
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Delaying…";
    m.requires.mayExtend = true;

    m.defaults = [] { return Settings { { "delay", Value (int64_t (3500)) },
                                        { "strength", Value (int64_t (70)) },
                                        { "units", Value (int64_t (Unit::Milliseconds)) } }; };
    m.schema = [] { return durationSchema (false); };

    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs) -> ModResult
    {
        const size_t frameSize = static_cast<size_t> (doc.frameSize());
        const Host::Limits limits = Host::getSampleValueLimits (doc.bps);
        const int bps = doc.bps;

        const size_t  delayBytes = alignedDelayBytes (getInt (prefs, "delay"), frameSize);
        const int64_t strength = getInt (prefs, "strength");
        const bool    doExtend = resolveDoExtend (host);

        size_t maxSelEnd = 0;

        const ModResult result = runSkeleton (doc, host, { "Delaying…", false },
                                              [&] (const TrackPass& pass, Progress& progress) -> ModResult
        {
            // Always exactly one delay-length, never a decay loop.
            size_t mixLimit = doExtend ? pass.end + delayBytes : pass.end;

            if (mixLimit > pass.size)
                mixLimit = std::min (mixLimit, growAndSilence (doc, pass.index, mixLimit));

            uint8_t* buf = doc.tracks[static_cast<size_t> (pass.index)].data.data();

            if (mixLimit <= pass.start + delayBytes) return kModNoError;
            if (mixLimit > maxSelEnd) maxSelEnd = mixLimit;

            // Backward: every read at dst−delayBytes precedes that position ever
            // being written, so it is always the untouched original. One clean copy.
            //
            // Signed, because firstDst can be 0 and the cursor must be allowed to
            // step off the bottom to end the loop — a size_t would wrap and run
            // forever.
            const int64_t step = static_cast<int64_t> (frameSize);
            const int64_t firstDst = static_cast<int64_t> (pass.start + delayBytes);

            int64_t dst = static_cast<int64_t> (mixLimit) - step;
            dst -= dst % step;

            for (; dst >= firstDst; dst -= step)
            {
                const size_t  target = static_cast<size_t> (dst);
                const int32_t here = getSample (buf, target - delayBytes, bps);

                int64_t ahead = getSample (buf, target, bps);
                ahead += (static_cast<int64_t> (here) * strength) / 100;

                if (ahead < limits.min) ahead = limits.min;
                else if (ahead > limits.max) ahead = limits.max;

                setSample (buf, target, static_cast<int32_t> (ahead), bps);

                if (progress.tick()) return kModNoError;
            }
            return kModNoError;
        });

        if (maxSelEnd > doc.selEnd) doc.selEnd = maxSelEnd;
        return result;
    };

    return m;
}

} // namespace se::modules
