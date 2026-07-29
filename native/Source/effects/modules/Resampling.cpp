// The Resampling group — Resample Rate, Downsample Bits, Upsample Bits.
// SPEC §8 #25/#7/#34. Ported from `src/effects/resampling.ts`.
//
// The theme: THESE MODULES IMPLEMENT NO CONVERSION AT ALL. Each validates,
// optionally pre-processes, and delegates to a host GlueRec service. The DSP lives
// in the app binary (engine/Resample.cpp), which is why recovering the host
// resamplers closed the last [ASM] DSP question rather than any of these.
//
// All three are whole-document operations. A partial selection triggers a
// confirmation alert ("Do you want me to proceed and downsample the whole
// sound?") — the host asks, and that alert belongs to step 7. With no alert layer
// to answer it, the C++ proceeds, exactly as the reference does when its optional
// `confirm` callback is absent.

#include "Modules.h"

#include "../Skeleton.h"
#include "../Units.h"
#include "engine/SampleFormat.h"

#include <cmath>

namespace se::modules {

namespace {

/**
 * Force a whole-document selection. Shared by all three modules.
 *
 * Note what it leaves behind: a CARET, not a full selection. The document is
 * converted end to end and the selection is reset to nothing, which is a
 * different final state from "everything selected".
 */
void requireWholeDocument (SoundDocument& doc)
{
    doc.selSt = 0;
    doc.selEnd = 0;
    doc.firstSelTrack = 1;
    doc.lastSelTrack = doc.numTracks();
}

/**
 * The noise-shaping pre-pass: `sample += trunc(Random()·noiseScale/32767)` over
 * every sample of every track, the selection having already been forced to the
 * whole document.
 *
 * noiseScale ≈ 2^bitsRemoved — the dither amplitude AUTO-SCALES to the size of the
 * discarded LSBs rather than being a fixed constant, which is the whole idea and
 * why the host's dither-free truncation is a reasonable division of labour. The
 * original routes an integer through a SANE float round-trip to get there; the
 * round-trip reads as a precision-rounding step, not a transcendental, so the
 * result is the power of two.
 */
void noiseShapePrePass (SoundDocument& doc, Host& host, int bitsRemoved)
{
    const size_t frameSize = static_cast<size_t> (doc.frameSize());
    const Host::Limits limits = Host::getSampleValueLimits (doc.bps);
    const double noiseScale = std::pow (2.0, static_cast<double> (bitsRemoved));

    for (auto& track : doc.tracks)
    {
        auto& buf = track.data;
        const size_t limit = buf.size() - (buf.size() % frameSize);

        for (size_t p = 0; p < limit; p += frameSize)
        {
            int64_t s = getSample (buf.data(), p, doc.bps);
            s += static_cast<int64_t> (
                std::trunc ((static_cast<double> (host.random.next()) * noiseScale) / 32767.0));

            if (s < limits.min) s = limits.min;
            else if (s > limits.max) s = limits.max;

            setSample (buf.data(), p, static_cast<int32_t> (s), doc.bps);
        }
    }
}

} // namespace

// --- Resample Rate -----------------------------------------------------------

EffectModule makeResampleRate()
{
    EffectModule m;
    m.name = "Resample Rate";
    m.signature = "ResampleRate";
    m.group = EffectGroup::Resampling;
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Resampling…";
    m.requires.wholeDocumentOnly = true;

    // "Settings default to 44.1 kHz (Fixed 44100.0) before any prefs are ever
    // saved." Stored as a 16.16 Fixed, which is what the field and the file
    // formats speak.
    m.defaults = [] { return Settings { { "newRate", Value (doubleToFixed (44100.0)) } }; };

    m.schema = []
    {
        // The dialog self-references the previous rate: "Resample the sound
        // (previously at N Hz) at [__] Hz", plus a rate-presets popup.
        return SettingsSchema {
            decimalField ("newRate", "Resample the sound at:"),
            staticText ("Hz"),
        };
    };

    // Real sample-rate conversion of the whole document, performed entirely by
    // ModResampleSoundRate. The interpolation is DROP-SAMPLE / zero-order-hold,
    // not linear — see engine/Resample.cpp.
    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs) -> ModResult
    {
        requireWholeDocument (doc);
        return host.resampleSoundRate (doc, fixedToDouble (getInt (prefs, "newRate")));
    };

    return m;
}

// --- Downsample Bits ---------------------------------------------------------

EffectModule makeDownsampleBits()
{
    EffectModule m;
    m.name = "Downsample Bits";
    m.signature = "DownsampleBits";
    m.group = EffectGroup::Resampling;
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Downsampling…";
    m.requires.wholeDocumentOnly = true;

    m.defaults = [] { return Settings { { "newBps", Value (int64_t (8)) },
                                        { "expert", Value (false) },
                                        { "expertMode", Value (int64_t (2)) },
                                        { "ditherLSB", Value (0.5) } }; };

    m.schema = []
    {
        return SettingsSchema {
            numberField ("newBps", "Downsample to:"),
            staticText ("bits"),
            checkBox ("expert", "Expert features"),
            radioGroup ("expertMode", { "Dither LSB in amplitude", "Noise shaping" }),
            decimalField ("ditherLSB", "LSB in amplitude"),
        };
    };

    // Sets bps lower. The truncation itself is ModResampleSoundBits; the module
    // owns validation and one optional pre-pass.
    //
    // OPEN SUB-QUESTION [BINARY: Downsample-Bits.md]: the exact radio binding. The
    // traced path has no user-value dependency, so it reads as "Noise shaping";
    // the "Dither [__] LSB" path was never located and may be dead code — which
    // would be entirely in character for the app that shipped a non-functional
    // Undo. Both radios run the traced path, and the doubt is recorded here.
    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs) -> ModResult
    {
        const int newBps = static_cast<int> (getInt (prefs, "newBps"));
        if (newBps >= doc.bps) return kModCancel;    // use Upsample Bits

        requireWholeDocument (doc);

        if (getBool (prefs, "expert"))
            noiseShapePrePass (doc, host, doc.bps - newBps);

        return host.resampleSoundBits (doc, newBps);
    };

    return m;
}

// --- Upsample Bits -----------------------------------------------------------

EffectModule makeUpsampleBits()
{
    EffectModule m;
    m.name = "Upsample Bits";
    m.signature = "UpsampleBits";
    m.group = EffectGroup::Resampling;
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Upsampling…";
    m.requires.wholeDocumentOnly = true;

    m.defaults = [] { return Settings { { "newBps", Value (int64_t (16)) } }; };

    m.schema = []
    {
        return SettingsSchema {
            numberField ("newBps", "Upsample to:"),
            staticText ("bits"),
        };
    };

    // Raises bps and nags you to use Downsample for reductions. The host shifts
    // samples left and zero-pads the new low bytes — no interpolation of
    // resolution, because there is none to invent.
    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs) -> ModResult
    {
        const int newBps = static_cast<int> (getInt (prefs, "newBps"));
        if (newBps <= doc.bps) return kModCancel;    // the nag: use Downsample

        requireWholeDocument (doc);
        return host.resampleSoundBits (doc, newBps);
    };

    return m;
}

} // namespace se::modules
