// Setups — named snapshots of an effect's settings, the "Setup:" popup's contents.
//
// In 0.9.2 the HOST owned them (disassembly/Setups.md): a module asked for the popup
// with ModDoSettingsDialog(withSetups = true), and the host saved the dialog's
// saveable items as a 'Setp' resource in the module's own file — durations as raw
// byte counts. Here a setup is keyed by setting name, and a duration is kept in
// SECONDS, so a setup means the same time on a 22 kHz 8-bit sound and a 48 kHz 24-bit
// one. That is the one departure; recall, matching and the factory list follow the
// original.
//
// Data and arithmetic only, no JUCE: the panel stores the list (AppPrefs) and draws
// the popup, and the state tests check what is here.

#pragma once

#include "Settings.h"
#include "Units.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace se {

struct Setup
{
    std::string name;
    /** Every saved key. A duration's value is its length in seconds (a Real). */
    Settings values;
};

/** What a module's setup holds: the keys it saves, and which of them are durations
    (held as bytes in the panel's settings, as seconds in a setup). */
struct SetupKeys
{
    std::vector<std::string> keys;
    std::set<std::string> durations;
};

/** A duration's bytes → seconds at a sound's rate and sample size. */
inline double setupSeconds (int64_t bytes, double rateHz, int bps) noexcept
{
    const auto frame = (int64_t) frameSizeFor (bps);
    return rateHz > 0.0 ? (double) (std::max<int64_t> (0, bytes) / frame) / rateHz : 0.0;
}

/** ...and back, to the NEAREST whole sample: unitToBytes truncates, and seconds that
    came from a whole number of samples must come back as that number. */
inline int64_t setupBytes (double seconds, double rateHz, int bps) noexcept
{
    return std::max<int64_t> (0, (int64_t) std::llround (seconds * rateHz)) * (int64_t) frameSizeFor (bps);
}

/** Sample Hose's "<group>.on" boxes are absent from older settings, and absent means on. */
inline bool isOnByDefaultKey (const std::string& key) noexcept
{
    return key.size() > 3 && key.compare (key.size() - 3, 3, ".on") == 0;
}

/** The setup for `working` as it stands, at the target's rate and sample size. */
inline Settings captureSetup (const Settings& working, const SetupKeys& keys, double rateHz, int bps)
{
    Settings out;
    for (const auto& key : keys.keys)
    {
        const auto it = working.find (key);
        if (it == working.end())
        {
            if (isOnByDefaultKey (key)) out[key] = Value (true);
            continue;
        }
        out[key] = keys.durations.count (key) != 0 ? Value (setupSeconds (it->second.asInt(), rateHz, bps))
                                                   : it->second;
    }
    return out;
}

/** Recall `setup` into `working`. Keys the setup does not hold keep their value. */
inline void applySetup (const Setup& setup, Settings& working, const SetupKeys& keys, double rateHz, int bps)
{
    for (const auto& [key, v] : setup.values)
        working[key] = keys.durations.count (key) != 0 ? Value (setupBytes (v.asDouble(), rateHz, bps)) : v;
}

/** Does `working` hold exactly what `setup` would recall? The original compared the
    whole record byte for byte; this compares what a recall would write. */
inline bool setupMatches (const Setup& setup, const Settings& working, const SetupKeys& keys,
                          double rateHz, int bps)
{
    for (const auto& [key, v] : setup.values)
    {
        const auto it = working.find (key);
        if (it == working.end())
        {
            if (isOnByDefaultKey (key) && v.asBool()) continue;
            return false;
        }

        const Value& w = it->second;
        if (keys.durations.count (key) != 0)
        {
            if (w.asInt() != setupBytes (v.asDouble(), rateHz, bps)) return false;
        }
        else if (v.kind == Value::Kind::Str || w.kind == Value::Kind::Str)
        {
            if (v.kind != w.kind || v.s != w.s) return false;
        }
        else if (std::abs (v.asDouble() - w.asDouble()) > 1.0e-9 * std::max (1.0, std::abs (v.asDouble())))
            return false;
    }
    return ! setup.values.empty();
}

/** The setups 0.9.2 shipped inside three module files, in their resource-map order
    (disassembly/Setups.md). Empty for every other module. */
inline std::vector<Setup> factorySetups (const std::string& moduleName)
{
    std::vector<Setup> out;

    if (moduleName == "Flanger")
    {
        // Byte counts of 8-bit samples at the "Standard" rate, units all milliseconds.
        struct F { const char* name; int pc; int delay, width, period; bool env; int depth; };
        const F f[] = {
            { "Plain Flange - Fast",    50, 134, 67,  17804, false, 0 },
            { "Plain Flange - Medium",  50, 245, 45,  22255, false, 0 },
            { "Plain Flange - Slow",    50, 245, 45,  50073, false, 0 },
            { "Plain Flange - Slower",  50, 245, 45,  77891, false, 0 },
            { "Plain Flange - Slowest", 50, 245, 45, 105710, false, 0 },
            { "Soft Flange",            50, 245, 45,  44510, false, 0 },
            { "Distorto - High",       100, 111, 89,   6676, true, 445 },
            { "Distorto - Medium",      70, 110, 88,  11127, true,  88 },
            { "Distorto - Low",         70, 111, 66,  13352, true,  44 },
        };
        const auto ms = (int64_t) Unit::Milliseconds;
        for (const auto& p : f)
        {
            Setup s;
            s.name = p.name;
            s.values = {
                { "flangePercent", Value ((int64_t) p.pc) },
                { "feedback", Value (false) },
                { "delay", Value ((double) p.delay / kRate22K) },   { "delayUnits", Value (ms) },
                { "width", Value ((double) p.width / kRate22K) },   { "widthUnits", Value (ms) },
                { "period", Value ((double) p.period / kRate22K) }, { "periodUnits", Value (ms) },
                { "modulation", Value ((int64_t) 1) },
                { "envelopeFollower", Value (p.env) },
                { "depth", Value ((double) p.depth / kRate22K) },   { "depthUnits", Value (ms) },
                { "louderMeans", Value ((int64_t) 1) },
            };
            out.push_back (std::move (s));
        }
    }
    else if (moduleName == "High Pass IIR" || moduleName == "Low Pass IIR")
    {
        // Two 68k extendeds each, a then b; byte-identical in both files.
        const struct { const char* name; double a, b; } f[] = {
            { "Medium", 0.5, 0.5 }, { "Low", 0.4, 0.6 }, { "Lower", 0.2, 0.7 },
        };
        for (const auto& p : f)
            out.push_back ({ p.name, { { "a", Value (p.a) }, { "b", Value (p.b) } } });
    }

    return out;
}

} // namespace se
