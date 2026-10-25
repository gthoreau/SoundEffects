// Sample Hose — SPEC §8 #28.
// [BINARY: disassembly/Sample-Hose-Iterative-Crossfade.md] for the kernels;
// [INTERPRETED] for the spray scheduling.
// Ported from `src/effects/sample-hose.ts`.
//
// A texture generator inspired by Fractal Design Painter's Image Hose: it sprays
// copies of the selection (or random "bites" of it) into new tracks. Five parameter
// types × five sub-params each = the famous wall of 27 numeric fields, which is
// worth reproducing faithfully in the UI.
//
// THE HEADLINE: RATE USES REAL LINEAR INTERPOLATION.
//
//     pos  = pos · (Rate/100)
//     idx  = trunc(pos);  frac = pos − idx
//     out  = src[idx] + frac·(src[idx+1] − src[idx])
//     out *= gain
//
// This is THE ONLY INTERPOLATING DSP ANYWHERE IN THE APP. Every Ricci module
// (Flanger, Echo, Delay) and the host's own resampler compute a fractional position
// and then throw the fraction away with a truncate. Norris — a third-party author —
// did it properly, which is why Sample Hose's pitch shifts are smooth where
// Resample Rate's are aliased stairsteps.
//
// As with Granular Synthesis: the kernels are verified, the SCHEDULING is not. The
// recovery is explicit that the offset→parameter mapping beyond Pan (50 = centre)
// and the curvature group is "inferred from the defaults and the DITL, not proven".
// See SPEC §14 item 21.

#include "Modules.h"
#include "Norris.h"

#include "../Rng.h"
#include "../Skeleton.h"
#include "../Units.h"
#include "engine/SampleFormat.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace se::modules {

namespace {

/** One of the five parameter types: Initial/Final Value, their Variances, Curvature. */
struct HoseParam
{
    double initialValue = 0.0;
    double initialVariance = 0.0;
    double finalValue = 0.0;
    double finalVariance = 0.0;
    double curvature = 100.0;   // 100 = linear
};

HoseParam readParam (const Settings& prefs, const std::string& key)
{
    HoseParam p;

    // 0.4.0: each of the five groups has an enabling checkbox in the design
    // ("<group>.on", absent = on, so every older setting reads as before). An
    // unchecked group sits at its factory resting value: no ramp and no variance.
    if (! getBool (prefs, key + ".on", true))
    {
        const double rest = key == "pan" ? 50.0 : key == "bite" ? 0.0 : key == "period" ? 10000.0 : 100.0;
        p.initialValue = p.finalValue = rest;
        return p;
    }

    p.initialValue    = getDouble (prefs, key + ".initialValue");
    p.initialVariance = getDouble (prefs, key + ".initialVariance");
    p.finalValue      = getDouble (prefs, key + ".finalValue");
    p.finalVariance   = getDouble (prefs, key + ".finalVariance");
    p.curvature       = getDouble (prefs, key + ".curvature", 100.0);
    return p;
}

/**
 * Value of a parameter at output position `n`, with its variance applied.
 *
 * The RNG is only consulted when the variance tween is non-zero, and THAT
 * CONDITION IS LOAD-BEARING: the five parameters are evaluated in a fixed order
 * (rate, gain, pan, bite, period) and share one generator, so a spurious draw for a
 * zero-variance parameter would shift every later value in the run.
 */
double paramAt (const HoseParam& p, double n, double length, AnsiCRandom& rng)
{
    const double base = tween (n, length, p.initialValue, p.finalValue, p.curvature);
    const double variance = tween (n, length, p.initialVariance, p.finalVariance, p.curvature);
    if (variance == 0.0) return base;

    const double jitter =
        (static_cast<double> (rng.next()) / static_cast<double> (AnsiCRandom::kRandMax)) * 2.0 - 1.0;
    return base + jitter * variance;
}

/** The five fields one parameter group contributes to the settings map. */
void addParamDefaults (Settings& s, const std::string& key, double initial, double final_)
{
    s[key + ".initialValue"]    = Value (initial);
    s[key + ".initialVariance"] = Value (0.0);
    s[key + ".finalValue"]      = Value (final_);
    s[key + ".finalVariance"]   = Value (0.0);
    s[key + ".curvature"]       = Value (100.0);
}

/**
 * The original's field types, read from the descriptor table in effect():
 * Bite and Period (and their Variances) are unit-dependent -- they follow the units
 * popup and are stored in bytes -- while Rate, Gain, Pan and every Curvature are
 * plain integers. Variance is in its value's unit, never a percentage.
 */
void addParamSchema (SettingsSchema& schema, const std::string& key, const std::string& label,
                     bool inUnits = false)
{
    const auto field = [inUnits] (std::string k, std::string l)
    {
        return inUnits ? durationField (std::move (k), std::move (l))
                       : numberField (std::move (k), std::move (l));
    };

    // The "%" after a field is the dialog's own static text (DITL): Rate, Gain and
    // Pan carry it on all four value/variance fields, Bite and Period on none (their
    // unit is the popup's), and every Curvature carries it. The panel glues a short
    // trailing static text onto the field's line.
    const auto percent = [inUnits] (SettingsSchema& sc)
    {
        if (! inUnits) sc.push_back (staticText ("%"));
    };

    // A HEADING, not a unit: it names the five fields below it. Left as plain
    // static text, the panel pulls it onto the previous group's last field and the
    // group loses its name.
    schema.push_back (staticHeading (label));
    schema.push_back (field (key + ".initialValue", "Initial Value"));
    percent (schema);
    schema.push_back (field (key + ".initialVariance", "Initial Variance"));
    percent (schema);
    schema.push_back (field (key + ".finalValue", "Final Value"));
    percent (schema);
    schema.push_back (field (key + ".finalVariance", "Final Variance"));
    percent (schema);
    schema.push_back (numberField (key + ".curvature", "Curvature"));
    schema.push_back (staticText ("%"));
}

} // namespace

EffectModule makeSampleHose()
{
    EffectModule m;
    m.name = "Sample Hose";
    m.signature = "SampleHose";
    m.group = EffectGroup::MikesModules;
    m.version = "1.0.3";
    m.author = "Michael Norris";
    m.progressText = "Hosing…";
    m.requires.createsTracks = true;

    /** The 106-byte settings handle's factory defaults, read from the binary. */
    m.defaults = []
    {
        Settings s {
            { "newLength", Value (int64_t (222000)) },
            { "units", Value (int64_t (Unit::Milliseconds)) },
            { "stereo", Value (true) },
        };
        addParamDefaults (s, "rate", 100.0, 100.0);
        addParamDefaults (s, "gain", 100.0, 100.0);
        addParamDefaults (s, "pan", 50.0, 50.0);      // centre
        addParamDefaults (s, "bite", 0.0, 0.0);       // 0 = whole sample
        addParamDefaults (s, "period", 10000.0, 10000.0);
        return s;
    };

    m.schema = []
    {
        SettingsSchema schema {
            durationField ("newLength", "New Length:"),
            unitsPopup ("units"),
        };
        addParamSchema (schema, "rate", "Rate");
        addParamSchema (schema, "gain", "Gain");
        addParamSchema (schema, "pan", "Pan");
        addParamSchema (schema, "bite", "Bite", true);
        addParamSchema (schema, "period", "Period", true);
        schema.push_back (checkBox ("stereo", "Stereo"));
        return schema;
    };

    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs) -> ModResult
    {
        expandEmptySelection (doc);
        host.setupProgress ("Hosing…");

        const size_t frameSize = static_cast<size_t> (doc.frameSize());
        const Host::Limits limits = Host::getSampleValueLimits (doc.bps);
        const int bps = doc.bps;

        AnsiCRandom rng (static_cast<uint32_t> (getInt (prefs, "seed", 1)));

        const HoseParam rateP = readParam (prefs, "rate");
        const HoseParam gainP = readParam (prefs, "gain");
        const HoseParam panP = readParam (prefs, "pan");
        const HoseParam biteP = readParam (prefs, "bite");
        const HoseParam periodP = readParam (prefs, "period");
        const bool stereo = getBool (prefs, "stereo");

        const int srcIdx = doc.firstSelTrack - 1;
        if (srcIdx < 0 || srcIdx >= doc.numTracks())
        {
            host.closeProgress();
            return kModCancel;
        }

        const std::vector<uint8_t>& src = doc.tracks[static_cast<size_t> (srcIdx)].data;
        const ByteRange range = trackRange (doc, srcIdx, doc.selSt, doc.selEnd);
        const int64_t srcFrames = static_cast<int64_t> ((range.end - range.start) / frameSize);
        const int64_t outFrames = static_cast<int64_t> (getInt (prefs, "newLength"))
                                / static_cast<int64_t> (frameSize);

        if (srcFrames < 2 || outFrames < 1)
        {
            host.closeProgress();
            return kModCancel;
        }

        const auto readSrc = [&] (int64_t i) -> double
        {
            if (i < 0 || i >= srcFrames) return 0.0;
            return static_cast<double> (
                getSample (src.data(), range.start + static_cast<size_t> (i) * frameSize, bps));
        };

        // Stereo sprays into two tracks, routed by Pan; mono into one.
        const size_t chans = stereo ? 2u : 1u;
        std::vector<std::vector<double>> acc (chans, std::vector<double> (static_cast<size_t> (outFrames), 0.0));

        const double outLen = static_cast<double> (outFrames);
        int64_t outPos = 0;
        int64_t guard = 0;

        while (outPos < outFrames && guard++ < outFrames * 4)
        {
            const double pos0 = static_cast<double> (outPos);

            // Order matters — one shared generator. See paramAt.
            const double rateFactor = paramAt (rateP, pos0, outLen, rng) / 100.0;
            const double gain = paramAt (gainP, pos0, outLen, rng) / 100.0;
            const double panPct = std::max (0.0, std::min (100.0, paramAt (panP, pos0, outLen, rng)));
            const double biteBytes = paramAt (biteP, pos0, outLen, rng);
            const double periodBytes = paramAt (periodP, pos0, outLen, rng);

            // Bite 0 = the whole sample; otherwise a random subsection of that length.
            const int64_t biteFrames = biteBytes > 0.0
                ? std::max<int64_t> (1, static_cast<int64_t> (std::trunc (biteBytes / static_cast<double> (frameSize))))
                : srcFrames;

            const int64_t biteStart = (biteBytes > 0.0 && srcFrames > biteFrames)
                ? static_cast<int64_t> (std::trunc (
                      (static_cast<double> (rng.next()) / static_cast<double> (AnsiCRandom::kRandMax))
                      * static_cast<double> (srcFrames - biteFrames)))
                : 0;

            // Pan: 0 = L, 50 = C, 100 = R.
            const double rightGain = stereo ? panPct / 100.0 : 1.0;
            const double leftGain = stereo ? 1.0 - rightGain : 1.0;

            // Spray one copy, resampled by Rate with real linear interpolation.
            double pos = 0.0;
            for (int64_t o = outPos; o < outFrames; ++o)
            {
                const int64_t idx = static_cast<int64_t> (std::trunc (pos));
                if (idx >= biteFrames - 1) break;

                const double frac = pos - static_cast<double> (idx);
                const double a = readSrc (biteStart + idx);
                const double b = readSrc (biteStart + idx + 1);

                // THE interpolation. Not a drop-sample read.
                const double v = (a + frac * (b - a)) * gain;

                if (chans == 2)
                {
                    acc[0][static_cast<size_t> (o)] += v * leftGain;
                    acc[1][static_cast<size_t> (o)] += v * rightGain;
                }
                else
                {
                    acc[0][static_cast<size_t> (o)] += v;
                }

                pos += rateFactor;
            }

            const int64_t hop = std::max<int64_t> (1,
                static_cast<int64_t> (std::trunc (periodBytes / static_cast<double> (frameSize))));
            outPos += hop;
        }

        for (const auto& a : acc)
        {
            std::vector<uint8_t> out (static_cast<size_t> (outFrames) * frameSize, silentByte (bps));
            for (int64_t i = 0; i < outFrames; ++i)
            {
                int64_t v = static_cast<int64_t> (std::trunc (a[static_cast<size_t> (i)]));
                if (v < limits.min) v = limits.min;
                else if (v > limits.max) v = limits.max;
                setSample (out.data(), static_cast<size_t> (i) * frameSize, static_cast<int32_t> (v), bps);
            }

            host.addNewTrack (doc);
            doc.tracks[static_cast<size_t> (doc.numTracks() - 1)].data = std::move (out);
        }

        host.closeProgress();
        return kModNoError;
    };

    return m;
}

} // namespace se::modules
