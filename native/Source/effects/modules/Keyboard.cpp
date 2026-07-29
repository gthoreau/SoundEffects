// Keyboard — SPEC §8 #18. [DOC+RES]
// Ported from `src/effects/keyboard.ts`.
//
// A sampler built out of nothing but a rate multiplier, and the app's whole
// personality in one button.
//
// You click a key; it plays the first selected track at that chromatic pitch by
// scaling the playback rate. Then CHANGE PITCH commits — by editing the document's
// rate field and touching NOT ONE BYTE of audio. The header now lies about the
// sound. If you want the lie to become true you go and run Resample Rate yourself;
// the app will not do it for you, and it will not warn you that it hasn't.
//
// effect() is the Change Pitch commit. In the original the menu item opens a window
// with keys in it and Change Pitch is a button inside it, so that window IS the
// settings dialog (step 7) and this is what OK does. Auditioning is the transport's
// job, not this module's.

#include "Modules.h"

#include "../Skeleton.h"
#include "../Units.h"

#include <cmath>

namespace se::modules {

namespace {

/**
 * "Only plays first selected channel (with an apologetic alert)." [DOC]
 *
 * [RES]-thin: the alert's existence and apologetic tone are documented, the exact
 * wording is not recovered. Phrased in the app's own first-person voice (§11) — if
 * the real string turns up, replace it.
 */
const char* kOneChannelOnly =
    "I'm sorry, but I can only play one channel at a time. I'll use the first selected one.";

/**
 * The rate multiplier for a chromatic offset: 2^(semitones/12).
 *
 * This is the entire synthesis engine. Twelve-tone equal temperament falls out of
 * a single pow(), and the "sampler" is a multiply.
 */
double pitchScale (int64_t semitones) noexcept
{
    return std::pow (2.0, static_cast<double> (semitones) / 12.0);
}

} // namespace

EffectModule makeKeyboard()
{
    EffectModule m;
    m.name = "Keyboard";
    m.signature = "Keyboard";
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "";
    m.about = "Click the keys to hear the first selected channel at different pitches. "
              "Change Pitch changes the sound's playback rate — it does not resample the "
              "sound. Use Resample Rate if you want the change to be real.";
    m.strings = { kOneChannelOnly };

    m.defaults = [] { return Settings { { "semitones", Value (int64_t (0)) },
                                        { "useSelectionForLooping", Value (false) } }; };

    m.schema = []
    {
        return SettingsSchema {
            numberField ("semitones", "Semitones:"),
            checkBox ("useSelectionForLooping", "Use selection for looping"),
        };
    };

    m.effect = [] (SoundDocument& doc, Host&, const Settings& prefs) -> ModResult
    {
        const int index = doc.firstSelTrack - 1;
        if (index < 0 || index >= doc.numTracks()) return kModCancel;

        const int64_t semitones = getInt (prefs, "semitones");
        // Unison is a no-op: there is no pitch to change.
        if (semitones == 0) return kModCancel;

        // Through Fixed and back, deliberately. The engine keeps rateHz as a
        // double, but the reference — and every file format — quantises to
        // 1/65536 Hz, and each commit lands on one of those ticks. Skipping the
        // round-trip would let twelve +1-semitone commits drift somewhere a
        // reloaded file could never go. doubleToFixed ROUNDS; truncating here
        // would bias every pitch change downward.
        doc.rateHz = fixedToDouble (doubleToFixed (doc.rateHz * pitchScale (semitones)));

        // kModNoError means "success, data modified" (§7.7) — and it is TRUE here
        // in the only sense the ABI cares about: the document changed. That not a
        // byte of audio moved is precisely the joke.
        return kModNoError;
    };

    return m;
}

} // namespace se::modules
