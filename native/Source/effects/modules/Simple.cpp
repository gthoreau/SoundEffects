// The published one-liners — SPEC §8 #8/#10/#11/#16/#22/#26/#31.
// ALL [DOC], none byte-verified. Ported from `src/effects/simple.ts`.
//
// §15's warning applies: "Smooth proved docs can lie, so treat as
// high-confidence-but-unproven." That is easy to accept for Invert and Reverse.
// It is less comfortable for Normalize and the fades, where the ROUNDING is
// unspecified and the house style (truncate, floor, integer-only) is inferred
// rather than read.

#include "Modules.h"

#include "../Skeleton.h"
#include "engine/SampleFormat.h"

#include <algorithm>
#include <cmath>

namespace se::modules {

namespace {
const char* kNoOptions = "Sorry, man. I haven't got any options.";
} // namespace

// --- Invert ------------------------------------------------------------------

EffectModule makeInvert()
{
    EffectModule m;
    m.name = "Invert";
    m.signature = "Invert";
    m.group = EffectGroup::Misc;
    m.version = "1.0";
    m.author = "Alberto Ricci";
    m.progressText = "Inverting…";
    m.strings = { kNoOptions };

    m.effect = [] (SoundDocument& doc, Host& host, const Settings&) -> ModResult
    {
        // Negating min (-128 at 8-bit) gives 128, which the clamp pins to 127 —
        // the asymmetry of two's complement, preserved rather than special-cased.
        return perSampleEffect (doc, host, { "Inverting…", false },
                                [] (int32_t s, const TrackPass&, size_t) -> int64_t
                                {
                                    return -static_cast<int64_t> (s);
                                });
    };

    return m;
}

// --- Reverse -----------------------------------------------------------------

EffectModule makeReverse()
{
    EffectModule m;
    m.name = "Reverse";
    m.signature = "Reverse";
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Reversing…";
    m.strings = { kNoOptions };
    // "You'll finally be able to hear those weird satanic messages hidden in your
    // sounds!" The low-memory path — which asks whether to proceed with a
    // possibly-incorrect reversal — needs a memory model, so it lands with the UI.

    m.effect = [] (SoundDocument& doc, Host& host, const Settings&) -> ModResult
    {
        const size_t frameSize = static_cast<size_t> (doc.frameSize());

        return runSkeleton (doc, host, { "Reversing…", false },
                            [frameSize] (const TrackPass& pass, Progress&) -> ModResult
        {
            const size_t frames = (pass.end - pass.start) / frameSize;
            for (size_t i = 0; i < frames / 2; ++i)
            {
                const size_t a = pass.start + i * frameSize;
                const size_t b = pass.start + (frames - 1 - i) * frameSize;
                for (size_t k = 0; k < frameSize; ++k)
                    std::swap (pass.data[a + k], pass.data[b + k]);
            }
            return kModNoError;
        });
    };

    return m;
}

// --- Silence -----------------------------------------------------------------

EffectModule makeSilence()
{
    EffectModule m;
    m.name = "Silence";
    m.signature = "Silence";
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Silencing…";
    // Replaces the selection with silence. With an insertion point the original
    // offers "Insert [__ units] of silence" instead — that is an EDITING
    // operation (engine/Edit.h has insertSilence), so this module is the replace
    // case only.

    m.effect = [] (SoundDocument& doc, Host& host, const Settings&) -> ModResult
    {
        const uint8_t silent = silentByte (doc.bps);

        return runSkeleton (doc, host, { "Silencing…", false },
                            [silent] (const TrackPass& pass, Progress&) -> ModResult
        {
            for (size_t p = pass.start; p < pass.end; ++p)
                pass.data[p] = silent;
            return kModNoError;
        });
    };

    return m;
}

// --- Draw Line ---------------------------------------------------------------

EffectModule makeDrawLine()
{
    EffectModule m;
    m.name = "Draw Line";
    m.signature = "DrawLine";
    m.group = EffectGroup::Misc;
    m.version = "1.0";
    m.author = "Alberto Ricci";
    m.progressText = "Drawing…";
    // Replaces the selection with a straight line from its first sample to its
    // last: a de-click tool, and an instant ramp generator.
    //
    // The Length field is a native addition — the original had no options here
    // (its voice said so: "Sorry, man. I haven't got any options."). 0 keeps the
    // recovered behaviour, the whole selection; a positive Length clamps to it,
    // as the fades' time does; and on a brand-new Window it grows the empty
    // tracks first, so a flat line is how a sound can start from nothing.
    m.requires.worksOnEmpty = true;

    m.defaults = [] { return Settings { { "length", Value (int64_t (0)) },
                                        { "units", Value (int64_t (Unit::Milliseconds)) } }; };
    m.schema = [] { return SettingsSchema { spanField ("length", "Length:"),
                                            unitsPopup ("units") }; };

    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs) -> ModResult
    {
        const size_t frameSize = static_cast<size_t> (doc.frameSize());
        const int bps = doc.bps;
        const int64_t length = getInt (prefs, "length");

        // Same dance as Waveform Generator: an empty track never reaches the
        // kernel, so grow it to the requested Length before the skeleton runs.
        const size_t wanted = length > 0
            ? static_cast<size_t> (length) - static_cast<size_t> (length) % frameSize
            : 0;
        if (wanted > 0)
            for (int t = doc.firstSelTrack - 1; t < doc.lastSelTrack; ++t)
                if (t >= 0 && t < doc.numTracks()
                    && doc.tracks[static_cast<size_t> (t)].data.empty())
                    growAndSilence (doc, t, wanted);

        return runSkeleton (doc, host, { "Drawing…", false },
                            [frameSize, bps, length] (const TrackPass& pass, Progress& progress) -> ModResult
        {
            const size_t available = pass.end - pass.start;
            const size_t span = length > 0
                ? std::min (static_cast<size_t> (length), available)
                : available;

            const size_t frames = span / frameSize;
            if (frames < 2) return kModNoError;

            const int32_t first = getSample (pass.data, pass.start, bps);
            const int32_t last  = getSample (pass.data, pass.start + (frames - 1) * frameSize, bps);

            for (size_t i = 0; i < frames; ++i)
            {
                // The reference truncates the WHOLE expression, not just the
                // division — `trunc(first + delta·i/(n-1))`. With a negative
                // first and a fractional part those are not the same number.
                const double exact = static_cast<double> (first)
                    + (static_cast<double> (last - first) * static_cast<double> (i))
                        / static_cast<double> (frames - 1);

                setSample (pass.data, pass.start + i * frameSize,
                           static_cast<int32_t> (std::trunc (exact)), bps);

                if (progress.tick()) return kModNoError;
            }
            return kModNoError;
        });
    };

    return m;
}

// --- Fade In / Fade Out ------------------------------------------------------

namespace {

SettingsSchema fadeSchema()
{
    return SettingsSchema {
        spanField ("time", "Fading time:"),
        unitsPopup ("units"),
    };
}

/**
 * Linear fades over `time` bytes.
 *
 * These shipped FPU and PowerPC variants — float-capable builds of the same
 * algorithm — which tells us the gain ramp was float on those paths. We take the
 * integer reading (s·i/n, truncating) because that is what the 68k build must
 * have done and what §2.2 calls the house discipline. A disassembly could refine
 * it; see SPEC §14.
 */
ModResult fadeEffect (SoundDocument& doc, Host& host, const Settings& prefs, bool fadeIn)
{
    const size_t frameSize = static_cast<size_t> (doc.frameSize());
    const int bps = doc.bps;
    const int64_t time = getInt (prefs, "time");
    const char* text = fadeIn ? "Fading in…" : "Fading out…";

    return runSkeleton (doc, host, { text, false },
                        [=] (const TrackPass& pass, Progress& progress) -> ModResult
    {
        // A fade time of 0 means the whole selection — otherwise the field would
        // have to be filled in before the module did anything at all.
        const size_t available = pass.end - pass.start;
        const size_t span = time > 0
            ? std::min (static_cast<size_t> (time), available)
            : available;

        const size_t frames = span / frameSize;
        if (frames < 1) return kModNoError;

        for (size_t i = 0; i < frames; ++i)
        {
            const size_t p = fadeIn ? pass.start + i * frameSize
                                    : pass.end - (i + 1) * frameSize;

            const int32_t s = getSample (pass.data, p, bps);
            const int64_t faded = (static_cast<int64_t> (s) * static_cast<int64_t> (i))
                                / static_cast<int64_t> (frames);

            setSample (pass.data, p, static_cast<int32_t> (faded), bps);

            if (progress.tick()) return kModNoError;
        }
        return kModNoError;
    });
}

} // namespace

EffectModule makeFadeIn()
{
    EffectModule m;
    m.name = "Fade In";
    m.signature = "FadeIn";
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Fading in…";
    m.defaults = [] { return Settings { { "time", Value (int64_t (0)) },
                                        { "units", Value (int64_t (Unit::Milliseconds)) } }; };
    m.schema = fadeSchema;
    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs)
    {
        return fadeEffect (doc, host, prefs, true);
    };
    return m;
}

EffectModule makeFadeOut()
{
    EffectModule m;
    m.name = "Fade Out";
    m.signature = "FadeOut";
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Fading out…";
    m.defaults = [] { return Settings { { "time", Value (int64_t (0)) },
                                        { "units", Value (int64_t (Unit::Milliseconds)) } }; };
    m.schema = fadeSchema;
    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs)
    {
        return fadeEffect (doc, host, prefs, false);
    };
    return m;
}

// --- Normalize ---------------------------------------------------------------

EffectModule makeNormalize()
{
    EffectModule m;
    m.name = "Normalize";
    m.signature = "Normalize";
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Normalizing…";

    // 1 = just the selected tracks, 2 = all tracks ("Norm All"). The multi-track
    // alert ("You are about to normalize only a part of this sound's channels…" —
    // Norm All / Just These / Cancel) is a HOST dialog; this setting stands in for
    // the answer. Note Norm All extends the selection to all tracks, NOT in time.
    m.defaults = [] { return Settings { { "scope", Value (int64_t (1)) } }; };

    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs) -> ModResult
    {
        if (getInt (prefs, "scope") == 2)
        {
            doc.firstSelTrack = 1;
            doc.lastSelTrack = doc.numTracks();
        }

        const int64_t max = Host::getSampleValueLimits (doc.bps).max;
        const int64_t peak = host.getPeak (doc).peak;

        if (peak == 0) return kModCancel;        // digital silence: nothing to scale

        // The gain is computed ONCE across all selected tracks, not per track —
        // otherwise normalising a stereo pair would destroy its balance. That is
        // the only reading consistent with "Norm All" being a meaningful choice,
        // but it is inference, not recovery.
        //
        // Integer percent, as Amplify takes: the house unit for gain.
        const int64_t gain = (max * 100) / peak;
        if (gain <= 100) return kModCancel;      // already at or over full scale

        return perSampleEffect (doc, host, { "Normalizing…", false },
                                [gain] (int32_t s, const TrackPass&, size_t) -> int64_t
                                {
                                    return (static_cast<int64_t> (s) * gain) / 100;
                                });
    };

    return m;
}

} // namespace se::modules
