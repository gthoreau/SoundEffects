// Robotize — SPEC §8 #27. [BINARY: disassembly/Robotize.md]
// Ported from `src/effects/robotize.ts`.
//
//     y[n] = clamp( x[n] + trunc( y[n−D] × 75 / 100 ) )
//     D    = trunc( 190 × rate / 22254.5454… )  samples
//
// A recursive feedback comb at a fixed ~117 Hz. The 190 is samples AT THE MAC'S
// NATIVE 22.254 kHz RATE, rescaled to the document's — so the delay is a constant
// ≈8.54 ms at any rate, and the comb fundamental is 22254.5454/190 ≈ 117.13 Hz.
// That pitch is the robot's voice; it does not track the sound, which is exactly
// why it sounds like a machine.

#include "Modules.h"

#include "../Skeleton.h"
#include "../Units.h"
#include "engine/SampleFormat.h"

#include <cmath>

namespace se::modules {

namespace {

/**
 * Fixed 0x56EE8BA3 = 22254 6/11 Hz — the classic Mac "Standard" rate, read
 * straight from the binary.
 *
 * The exact constant matters, not a decimal approximation of it: the truncation
 * below is sharper than it looks, and a rate one 1/65536th BELOW this gives
 * trunc(190 × 0.99999…) = 189, not 190.
 */
constexpr Fixed kNativeRate = 0x56ee8ba3;
/** Word at code offset 0x8d2. */
constexpr int64_t kBaseDelaySamples = 190;
/** The feedback amount, integer percent. Not a parameter — Robotize has none. */
constexpr int64_t kFeedbackPercent = 75;

} // namespace

EffectModule makeRobotize()
{
    EffectModule m;
    m.name = "Robotize";
    m.signature = "Robotize";
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Robotizing sound…";

    m.effect = [] (SoundDocument& doc, Host& host, const Settings&) -> ModResult
    {
        const size_t frameSize = static_cast<size_t> (doc.frameSize());
        const Host::Limits limits = Host::getSampleValueLimits (doc.bps);
        const int bps = doc.bps;

        // Computed once per run. The original goes through SANE extended precision
        // and truncates to integral; at exactly the native rate the division
        // cancels and D is 190 on the nose.
        const int64_t delaySamples = static_cast<int64_t> (
            std::trunc ((static_cast<double> (kBaseDelaySamples) * doc.rateHz)
                        / fixedToDouble (kNativeRate)));
        const size_t delayBytes = static_cast<size_t> (delaySamples) * frameSize;

        return runSkeleton (doc, host, { "Robotizing sound…", false },
                            [=] (const TrackPass& pass, Progress& progress) -> ModResult
        {
            // Selections shorter than the delay (~8.5 ms) are a silent no-op — the
            // loop simply never runs. The first D samples are the untouched seed.
            if (pass.end < delayBytes) return kModNoError;

            for (size_t i = pass.start; i < pass.end - delayBytes; i += frameSize)
            {
                // Reads the CURRENT, possibly already-modified value: this forward
                // pass is genuinely recursive, and one pass produces the whole
                // comb — the same trick Echo uses.
                const int32_t here = getSample (pass.data, i, bps);
                const size_t target = i + delayBytes;

                int64_t ahead = getSample (pass.data, target, bps);
                ahead += (static_cast<int64_t> (here) * kFeedbackPercent) / 100;

                if (ahead < limits.min) ahead = limits.min;
                else if (ahead > limits.max) ahead = limits.max;

                setSample (pass.data, target, static_cast<int32_t> (ahead), bps);

                if (progress.tick()) return kModNoError;
            }
            return kModNoError;
        });
    };

    return m;
}

} // namespace se::modules
