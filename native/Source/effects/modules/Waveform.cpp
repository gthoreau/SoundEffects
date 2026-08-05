// Waveform Generator — SPEC §8 #35. [RES], not byte-verified.
// Ported from `src/effects/waveform.ts`.
//
// Synthesises into the selection: Length, Frequency (Hz), Amplitude (%), and a
// Sine / Square / Sawtooth radio.
//
// [INTERPRETED]: the DITL gives the fields; nothing gives the waveform
// conventions. Sawtooth's direction and phase, whether the amplitude percentage is
// of the bit depth's max or of 2^(bps−1) (Dither uses the latter, and is off by one
// for it), and whether Length extends the selection or is clamped to it — all
// inference. See SPEC §14.

#include "Modules.h"

#include "../Skeleton.h"
#include "../Units.h"
#include "engine/SampleFormat.h"

#include <algorithm>
#include <cmath>

namespace se::modules {

namespace {

constexpr int64_t kSine = 1, kSquare = 2, kSawtooth = 3;

/** One cycle of the chosen wave at phase `p` (0..1), in −1..1. */
double waveSample (int64_t waveType, double p) noexcept
{
    switch (waveType)
    {
        case kSine:     return std::sin (2.0 * M_PI * p);
        // Zero counts as positive, matching Weird Tempo's sign convention.
        case kSquare:   return p < 0.5 ? 1.0 : -1.0;
        // Rising ramp −1 → 1, reset. Direction is inference.
        case kSawtooth: return 2.0 * p - 1.0;
        default:        return 0.0;
    }
}

} // namespace

EffectModule makeWaveformGenerator()
{
    EffectModule m;
    m.name = "Waveform Generator";
    m.signature = "WaveformGenerator";
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Generating…";

    m.defaults = [] { return Settings { { "length", Value (int64_t (0)) },
                                        { "units", Value (int64_t (Unit::Milliseconds)) },
                                        { "frequency", Value (double (440.0)) },
                                        { "amplitude", Value (int64_t (100)) },
                                        { "waveType", Value (kSine) } }; };

    m.schema = []
    {
        return SettingsSchema {
            spanField ("length", "Length:"),
            unitsPopup ("units"),
            decimalField ("frequency", "Frequency:"),
            staticText ("Hz"),
            numberField ("amplitude", "Amplitude:"),
            staticText ("%"),
            radioGroup ("waveType", { "Sine", "Square", "Sawtooth" }),
        };
    };

    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs) -> ModResult
    {
        const size_t frameSize = static_cast<size_t> (doc.frameSize());
        const Host::Limits limits = Host::getSampleValueLimits (doc.bps);
        const int bps = doc.bps;
        const double rate = doc.rateHz;

        const int64_t length = getInt (prefs, "length");
        const double  frequency = getDouble (prefs, "frequency");
        const int64_t waveType = getInt (prefs, "waveType");

        // Amplitude as a fraction of the depth's max, kept as a double. Dither
        // scales by 2^(bps−1) instead and overshoots by one; which convention this
        // module uses is not recorded, so we take the safe one that cannot clip on
        // its own.
        const double amp = (static_cast<double> (limits.max) * static_cast<double> (getInt (prefs, "amplitude")))
                         / 100.0;

        return runSkeleton (doc, host, { "Generating…", false },
                            [=] (const TrackPass& pass, Progress& progress) -> ModResult
        {
            const size_t available = pass.end - pass.start;
            const size_t span = length > 0
                ? std::min (static_cast<size_t> (length), available)
                : available;

            const size_t frames = span / frameSize;
            if (frames < 1 || rate <= 0.0) return kModNoError;

            const double cyclesPerSample = frequency / rate;

            for (size_t i = 0; i < frames; ++i)
            {
                const double phase = std::fmod (static_cast<double> (i) * cyclesPerSample, 1.0);

                // Truncating, as the house style demands even where the maths is
                // float.
                int64_t v = static_cast<int64_t> (std::trunc (amp * waveSample (waveType, phase)));
                if (v < limits.min) v = limits.min;
                else if (v > limits.max) v = limits.max;

                setSample (pass.data, pass.start + i * frameSize, static_cast<int32_t> (v), bps);

                if (progress.tick()) return kModNoError;
            }
            return kModNoError;
        });
    };

    return m;
}

} // namespace se::modules
