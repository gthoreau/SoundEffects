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
 * STR# 1000, verbatim. [RES]
 *
 * These were placeholders in the app's own voice until the shipping module's
 * resource fork was read: `SoundEffects 0.9.2 ƒ/Effects Folder/Keyboard` still
 * carries STR# 1000, and these are its strings, index for index, typo and curly
 * apostrophes and all — "Keboard" is the original's own slip and stays.
 *
 * NOTHING RAISES #3 OR #5 ANY MORE, and that is deliberate rather than an
 * oversight. #3 is the apology for playing only the first selected channel — the
 * Keyboard palette plays every selected track, so the limitation it apologises for
 * no longer exists. #5 warns about an empty sound; the palette says "NO AUDIO" in
 * its own readout instead of interrupting with a box. They stay here because
 * `strings` is the module's recovered STR# 1000 indexed by result code (§7.7) —
 * it is ABI data, not a to-do list, and a module returning 3 would still speak.
 */
const char* kNoSettings = "The Keyboard module does not have any settings.";
const char* kOneChannelOnly =
    "I\xe2\x80\x99m sorry, but with this version of the Keboard module you will be able to "
    "play the first selected channel only.\n"
    "You might want to mix the sound to a single channel next time.";
const char* kNoChannel =
    "I couldn\xe2\x80\x99t open a sound channel to work with. Either there is not enough "
    "memory, or there are too many sounds already playing.";
const char* kEmptySound =
    "You are using the Keyboard module with an empty sound. You won\xe2\x80\x99t hear anything.";

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
    // TEXT 1000, verbatim. [RES] — recovered from the shipping module alongside
    // STR# 1000, replacing a paraphrase written before the resource fork was read.
    m.about =
        "The Keyboard module lets you play the selected sound at various pitches, depending "
        "on where you click on a piano keyboard.\n"
        "If more than one channel is selected when you use this module, only the first "
        "selected channel will play.\n\n"
        "If you click the \xe2\x80\x9c""Change Pitch\xe2\x80\x9d button, the pitch of the sound "
        "will be changed to the one that corresponds to the last clicked piano key. To change "
        "the pitch of the sound, however, the module does not change the sound data itself. "
        "Instead, it modifies the playback rate.\n"
        "So, if you don\xe2\x80\x99t want the playback rate to be changed, you will have to do a "
        "\xe2\x80\x9c""Resample Rate\xe2\x80\x9d effect on it, specifying the original rate.\n\n"
        "Thanks to Hoby Van Hoose.";
    // Indexed by |result| (§7.7), so the empty slot at #2 is load-bearing.
    m.strings = { kNoSettings, "", kOneChannelOnly, kNoChannel, kEmptySound };

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
