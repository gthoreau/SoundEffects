// Flanger — SPEC §8 #12. [BINARY: disassembly/Flanger.md]
// Ported from `src/effects/flanger.ts`.
//
// A modulated-delay comb: a delayed copy mixed with the original, the delay swept
// over Period by an LFO between Delay±Width. The most expensive module in the app
// and — despite that — still non-cancellable.
//
// Four recovered facts that shape this port:
//
//  1. NO INTERPOLATION. The delay position is computed to full 80-bit SANE-extended
//     precision and then fintrz'd to a single integer byte offset before the
//     one-and-only delay-line read. Sub-sample precision is computed and thrown
//     away. (Sample Hose is the app's ONLY interpolating DSP.)
//  2. FEEDBACK IS A SWEEP-DIRECTION FLIP, NOT A TAP. The whole per-sample routine
//     is compiled twice. Feedback off walks BACKWARD (every read touches pristine
//     history — Delay's trick); on walks FORWARD (the sweep eventually reads its
//     own already-mixed output — Echo's trick). There is no feedback-gain
//     constant; the recursion's gain is just the wet weight.
//  3. LFO: Sinusoidal is a real sin(), not a table. Triangular and Sawtooth are
//     linear accumulators — reflect vs. hard-reset.
//  4. ENVELOPE FOLLOWER IS BLOCK-PEAK, not RMS and not a one-pole smoother:
//     ModGetPeak over 1024-sample windows, linearly interpolated between blocks.
//
// The final mix, in contrast to all that float, is plain truncating integer math —
// the house convention holds even here.
//
// Fixed-length and in-place: no doExtend, no SetHandleSize, selEnd untouched.

#include "Modules.h"

#include "../Skeleton.h"
#include "../Units.h"
#include "engine/SampleFormat.h"

#include <algorithm>
#include <cmath>

namespace se::modules {

namespace {

constexpr int64_t kSinusoidal = 1, kTriangular = 2, kSawtooth = 3;
constexpr int64_t kLouderLonger = 1;

/** The block size the envelope follower scans over. */
constexpr int64_t kEnvelopeBlockSamples = 1024;

/**
 * The LFO — returns the delay offset in BYTES, before truncation.
 *
 * [INTERPRETED]: the accumulators' initial values aren't in the recovery. Sawtooth
 * hard-resets to Delay−Width, so starting there is the only self-consistent
 * choice; triangular starts at the centreline rising, which matches Sinusoidal's
 * phase-0 value and makes the three waveforms agree at n=0. See SPEC §14.
 */
class FlangerLFO
{
public:
    FlangerLFO (int64_t modulationIn, double delayIn, double widthIn, double periodSamplesIn) noexcept
        : modulation (modulationIn), delay (delayIn), width (widthIn),
          periodSamples (periodSamplesIn),
          acc (modulationIn == kSawtooth ? delayIn - widthIn : delayIn)
    {
    }

    /** `n` = samples processed so far in this track. */
    double next (int64_t n) noexcept
    {
        switch (modulation)
        {
            case kSinusoidal:
                // Phase is driven by a plain sample counter — not wall-clock, not
                // a fractional-Hz phase accumulator.
                return delay + width * std::sin ((2.0 * M_PI * static_cast<double> (n)) / periodSamples);

            case kTriangular:
            {
                const double v = acc;
                const double step = (4.0 * width) / periodSamples;
                acc += dir * step;
                // Reflect at the rails.
                if (acc > delay + width)      { acc = delay + width; dir = -1.0; }
                else if (acc < delay - width) { acc = delay - width; dir =  1.0; }
                return v;
            }

            case kSawtooth:
            {
                const double v = acc;
                acc += (2.0 * width) / periodSamples;
                // Hard reset, not reflected.
                if (acc > delay + width) acc = delay - width;
                return v;
            }

            default:
                return delay;
        }
    }

private:
    int64_t modulation;
    double  delay, width, periodSamples;
    double  acc;
    double  dir = 1.0;
};

/** Peak of one block of a track, as ModGetPeak would report it. */
int64_t blockPeak (const uint8_t* data, size_t dataSize, int64_t from, int64_t to, int bps) noexcept
{
    const int64_t frameSize = frameSizeFor (bps);
    const int64_t hi = std::min (to, static_cast<int64_t> (dataSize - (dataSize % static_cast<size_t> (frameSize))));

    int64_t peak = 0;
    for (int64_t p = std::max<int64_t> (0, from); p + frameSize <= hi; p += frameSize)
    {
        const int64_t v = std::abs (static_cast<int64_t> (getSample (data, static_cast<size_t> (p), bps)));
        if (v > peak) peak = v;
    }
    return peak;
}

} // namespace

EffectModule makeFlanger()
{
    EffectModule m;
    m.name = "Flanger";
    m.signature = "Flanger";
    m.group = EffectGroup::EchoesAndReverbs;
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Flanging…";
    m.strings = { "", "",
                  "The Flanger effect can only be applied to an existing sound. Please open or "
                  "record one before trying to apply the effect again." };

    // The 60-byte in-memory struct's hard-coded fallbacks. Delay/Width/Period/Depth
    // are left at whatever NewHandle returned in the original — "a full
    // extended-precision default wasn't worth hand-coding" — so we supply the
    // Plain Flange - Medium values rather than ship uninitialised garbage.
    m.defaults = [] { return Settings { { "flangePercent", Value (int64_t (80)) },
                                        { "feedback", Value (false) },
                                        { "delay", Value (int64_t (245)) },
                                        { "delayUnits", Value (int64_t (Unit::Milliseconds)) },
                                        { "width", Value (int64_t (45)) },
                                        { "widthUnits", Value (int64_t (Unit::Milliseconds)) },
                                        { "period", Value (int64_t (22255)) },
                                        { "periodUnits", Value (int64_t (Unit::Milliseconds)) },
                                        { "modulation", Value (kSinusoidal) },
                                        { "envelopeFollower", Value (false) },
                                        { "depth", Value (int64_t (0)) },
                                        { "depthUnits", Value (int64_t (Unit::Milliseconds)) },
                                        { "louderMeans", Value (kLouderLonger) } }; };

    m.schema = []
    {
        return SettingsSchema {
            numberField ("flangePercent", "Flange percentage:", 0, 100),
            durationField ("delay", "Delay:"),
            durationField ("width", "Width:"),
            durationField ("period", "Period:"),
            unitsPopup ("delayUnits"),
            checkBox ("feedback", "Feedback"),
            radioGroup ("modulation", { "Sinusoidal", "Triangular", "Sawtooth" }),
            checkBox ("envelopeFollower", "Envelope Follower"),
            durationField ("depth", "Depth:"),
            radioGroup ("louderMeans", { "Longer", "Shorter" }),
        };
    };

    /** 0x32 is read, clamped to [0,100], and 100 − value cached as the dry weight. */
    m.validate = [] (Settings s)
    {
        s["flangePercent"] = Value (std::max<int64_t> (0, std::min<int64_t> (100, getInt (s, "flangePercent"))));
        return s;
    };

    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs) -> ModResult
    {
        const size_t frameSize = static_cast<size_t> (doc.frameSize());
        const Host::Limits limits = Host::getSampleValueLimits (doc.bps);
        const int bps = doc.bps;

        const int64_t wet = std::max<int64_t> (0, std::min<int64_t> (100, getInt (prefs, "flangePercent")));
        const int64_t dryWeight = 100 - wet;

        const int64_t delayBytes = getInt (prefs, "delay");
        const int64_t widthBytes = getInt (prefs, "width");
        const int64_t depth = getInt (prefs, "depth");
        const int64_t modulation = getInt (prefs, "modulation");
        const int64_t louderMeans = getInt (prefs, "louderMeans");
        const bool    feedback = getBool (prefs, "feedback");
        const bool    envelopeFollower = getBool (prefs, "envelopeFollower");

        // A DOUBLE division: the period is a byte count and the LFO wants samples,
        // and the reference never rounds it to an integer.
        const double periodSamples = std::max (1.0, static_cast<double> (getInt (prefs, "period"))
                                                        / static_cast<double> (frameSize));
        const int64_t blockBytes = kEnvelopeBlockSamples * static_cast<int64_t> (frameSize);

        return runSkeleton (doc, host, { "Flanging…", false },
                            [=] (const TrackPass& pass, Progress& progress) -> ModResult
        {
            // The first Delay+Width bytes are consumed as delay-line seed and never
            // become an output position — though their audio is still readable as
            // history for later positions.
            size_t start = pass.start + static_cast<size_t> (delayBytes + widthBytes);
            start -= start % frameSize;
            if (start >= pass.end) return kModNoError;

            uint8_t* buf = pass.data;

            // The fixed reference peak: the last <=1024 samples of the selection,
            // scanned once before the loop.
            const int64_t refPeak = envelopeFollower
                ? blockPeak (buf, pass.size,
                             std::max (static_cast<int64_t> (start),
                                       static_cast<int64_t> (pass.end) - blockBytes),
                             static_cast<int64_t> (pass.end), bps)
                : 0;

            FlangerLFO lfo (modulation, static_cast<double> (delayBytes),
                            static_cast<double> (widthBytes), periodSamples);

            // Feedback OFF -> backward (reads only pristine history).
            // Feedback ON  -> forward  (eventually reads its own mixed output).
            const bool    forward = feedback;
            const int64_t frames = static_cast<int64_t> ((pass.end - start) / frameSize);

            int64_t chunkPeak = refPeak;
            int64_t currentBlock = -1;

            for (int64_t n = 0; n < frames; ++n)
            {
                const int64_t cursor = forward
                    ? static_cast<int64_t> (start) + n * static_cast<int64_t> (frameSize)
                    : static_cast<int64_t> (pass.end) - static_cast<int64_t> (frameSize)
                          - n * static_cast<int64_t> (frameSize);

                // 1. Dry read, at the write cursor, before any delay offset.
                const int64_t dry = getSample (buf, static_cast<size_t> (cursor), bps);

                // 2. LFO -> float delay offset, then truncate toward zero. This is
                //    the fintrz that throws away all the precision above.
                int64_t d3 = static_cast<int64_t> (std::trunc (lfo.next (n)));

                // 3. Envelope follower.
                if (envelopeFollower)
                {
                    const int64_t block = n / kEnvelopeBlockSamples;
                    if (block != currentBlock)
                    {
                        currentBlock = block;
                        const int64_t from = forward
                            ? static_cast<int64_t> (start) + block * blockBytes
                            : static_cast<int64_t> (pass.end) - (block + 1) * blockBytes;
                        chunkPeak = blockPeak (buf, pass.size, from, from + blockBytes, bps);
                    }

                    const int64_t chunkPos = n % kEnvelopeBlockSamples;
                    const double interpolatedPeak =
                        static_cast<double> (refPeak)
                        + (static_cast<double> (chunkPeak - refPeak) * static_cast<double> (chunkPos))
                              / static_cast<double> (kEnvelopeBlockSamples);

                    const int64_t adj = static_cast<int64_t> (
                        std::trunc ((interpolatedPeak * static_cast<double> (depth))
                                    / static_cast<double> (limits.max)));

                    if (louderMeans == kLouderLonger) d3 += adj;
                    else                              d3 -= adj;
                }

                // 4. The delay can never go negative — it must never read ahead of
                //    the write cursor.
                if (d3 < 0) d3 = 0;
                // [INTERPRETED]: frame-align the wet read. The recovery shows the
                // 8-bit path reading one byte at cursor − d3 with no alignment
                // step, which is moot at frameSize 1; whether the wide path aligns
                // is not recorded. Reading unaligned would splice two frames into
                // garbage, so we align. See SPEC §14.
                d3 -= d3 % static_cast<int64_t> (frameSize);

                // 5. Wet read.
                const int64_t src = cursor - d3;
                const int64_t wetSample =
                    (src >= 0 && src + static_cast<int64_t> (frameSize) <= static_cast<int64_t> (pass.size))
                        ? getSample (buf, static_cast<size_t> (src), bps)
                        : 0;

                // 6. Mix — plain truncating integer math, despite everything above.
                int64_t out = (dry * dryWeight) / 100 + (wetSample * wet) / 100;
                if (out < limits.min) out = limits.min;
                else if (out > limits.max) out = limits.max;

                // 7. Replace the dry sample in place; do not add to it.
                setSample (buf, static_cast<size_t> (cursor), static_cast<int32_t> (out), bps);

                if (progress.tick()) return kModNoError;
            }
            return kModNoError;
        });
    };

    return m;
}

} // namespace se::modules
