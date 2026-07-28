// The three IIR filters — High Pass IIR, Low Pass IIR, Shelving.
// SPEC §8 #15/#20/#30. [BINARY: disassembly/Dither-IIR-Filters.md]
// Ported from `src/effects/iir.ts`.
//
//     High Pass  y(n) = (A·x(n))/10 − (B·y(n−1))/10
//     Low Pass   y(n) = (A·x(n))/10 + (B·y(n−1))/10
//     Shelving   y(n) = x(n) + (A·x(n−1))/10 + (B·y(n−1))/10
//
// The published equations are y(n) = a·x(n) ± b·y(n−1) and they hold — but the
// UNWRITTEN details are where the sound lives, and there are three of them:
//
//   1. Coefficients are QUANTISED TO ONE DECIMAL PLACE AND TRUNCATED:
//      A = trunc(a·10). Type 0.55, get 0.5.
//   2. Each term is TRUNCATED INDEPENDENTLY by its own integer divide — and in a
//      recursive filter that error feeds back forever.
//   3. y(n−1) IS SEEDED WITH THE FIRST SAMPLE, not zero, and that first sample is
//      left unmodified.
//
// There is NO COEFFICIENT RANGE CHECKING anywhere. Enter b = 2.0 and the filter
// blows up sample by sample, clamping into a square wave. That is the intended
// experience, and validating it away would be a bug.

#include "Modules.h"

#include "../Skeleton.h"
#include "engine/SampleFormat.h"

#include <cmath>

namespace se::modules {

namespace {

enum class IIRKind { HighPass, LowPass, Shelving };

SettingsSchema iirSchema()
{
    return SettingsSchema {
        staticText ("Coefficients"),
        decimalField ("a", "a:"),
        decimalField ("b", "b:"),
    };
}

/**
 * The shared kernel. The three modules differ only in the inner expression —
 * `add.l` vs `sub.l` vs two adds, one instruction apart in the original.
 */
ModResult iirEffect (SoundDocument& doc, Host& host, const Settings& prefs, IIRKind kind)
{
    const size_t frameSize = static_cast<size_t> (doc.frameSize());
    const Host::Limits limits = Host::getSampleValueLimits (doc.bps);
    const int bps = doc.bps;

    // Quantise to one decimal and truncate — this is what settings() stored.
    const int64_t A = static_cast<int64_t> (std::trunc (getDouble (prefs, "a") * 10.0));
    const int64_t B = static_cast<int64_t> (std::trunc (getDouble (prefs, "b") * 10.0));

    return runSkeleton (doc, host, { "Filtering…", false },
                        [=] (const TrackPass& pass, Progress& progress) -> ModResult
    {
        const int32_t first = getSample (pass.data, pass.start, bps);

        // Seeded with the signal's own first value, not silence: no start-up
        // transient, but the first sample passes through unmodified.
        int64_t yprev = first;
        int64_t xprev = first;

        for (size_t p = pass.start + frameSize; p < pass.end; p += frameSize)
        {
            const int64_t x = getSample (pass.data, p, bps);

            int64_t y = 0;
            switch (kind)
            {
                case IIRKind::LowPass:
                    y = (A * x) / 10 + (B * yprev) / 10;
                    break;
                case IIRKind::HighPass:
                    y = (A * x) / 10 - (B * yprev) / 10;
                    break;
                case IIRKind::Shelving:
                    // The feed-forward tap uses the ORIGINAL input, saved before
                    // the write — pristine input, not filtered output.
                    y = x + (A * xprev) / 10 + (B * yprev) / 10;
                    break;
            }

            if (y < limits.min) y = limits.min;
            else if (y > limits.max) y = limits.max;

            setSample (pass.data, p, static_cast<int32_t> (y), bps);
            yprev = y;
            xprev = x;

            if (progress.tick()) return kModNoError;
        }
        return kModNoError;
    });
}

EffectModule iirModule (std::string name, std::string signature, double a, double b)
{
    EffectModule m;
    m.name = std::move (name);
    m.signature = std::move (signature);
    m.group = EffectGroup::FirIirFilters;
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Filtering…";
    m.defaults = [a, b] { return Settings { { "a", Value (a) }, { "b", Value (b) } }; };
    m.schema = iirSchema;
    return m;
}

} // namespace

EffectModule makeHighPassIIR()
{
    EffectModule m = iirModule ("High Pass IIR", "HighPassIIR", 1.0, 0.5);
    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs)
    {
        return iirEffect (doc, host, prefs, IIRKind::HighPass);
    };
    return m;
}

EffectModule makeLowPassIIR()
{
    EffectModule m = iirModule ("Low Pass IIR", "LowPassIIR", 0.5, 0.5);
    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs)
    {
        return iirEffect (doc, host, prefs, IIRKind::LowPass);
    };
    return m;
}

EffectModule makeShelving()
{
    EffectModule m = iirModule ("Shelving", "Shelving", 0.5, 0.5);
    // Its help text ends mid-sentence in TEXT 1000. Ship the truncated sentence.
    m.about = "The response is that of a shelving filter, with.";
    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs)
    {
        return iirEffect (doc, host, prefs, IIRKind::Shelving);
    };
    return m;
}

} // namespace se::modules
