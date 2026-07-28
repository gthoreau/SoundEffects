// Amplify — s' = clamp(s × gain / 100). SPEC §8 #1, §7.8.
//
// [SOURCE] — the SDK's Gain.c IS this module, so it is the one effect that can be
// ported from real C rather than from a disassembly or a doc. That makes it the
// reference: if the skeleton is shaped right, this file is nearly all metadata.
//
// Ported from the TS reference `src/effects/amplify.ts`.

#include "Modules.h"

#include "../Skeleton.h"

namespace se::modules {

EffectModule makeAmplify()
{
    EffectModule m;
    m.name = "Amplify";
    m.signature = "SampleEffect";
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Amplifying by ";

    m.defaults = [] { return Settings { { "gain", Value (int64_t (200)) } }; };

    m.schema = []
    {
        return SettingsSchema {
            numberField ("gain", "Amplify by:", 0, 32767),
            staticText ("%"),
        };
    };

    /** Gain.c's `settings` clamps to a short's range. */
    m.validate = [] (Settings s)
    {
        s["gain"] = Value (std::min<int64_t> (getInt (s, "gain"), 32767));
        return s;
    };

    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs) -> ModResult
    {
        const int64_t gain = getInt (prefs, "gain");

        // `mySample = mySample * myGain / 100` — INTEGER, truncating. The rounding
        // scar is the point (§2.2), so this must never become a rounded multiply.
        // 64-bit because a 32-bit sample times a 32767 gain overflows an int32.
        return perSampleEffect (doc, host, { "Amplifying by ", false },
                                [gain] (int32_t s, const TrackPass&, size_t) -> int64_t
                                {
                                    return (static_cast<int64_t> (s) * gain) / 100;
                                });
    };

    return m;
}

} // namespace se::modules
