#include "RecordConvert.h"

#include "SampleFormat.h"

#include <algorithm>
#include <cmath>

namespace se {

namespace {

constexpr double kPi = 3.14159265358979323846;

double bessel0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1.0e-14 * sum) break;
    }
    return sum;
}

} // namespace

RecordConverter::RecordConverter (double inR, double outR, int outBps, ConvertMode m, uint32_t seed)
    : inRate (std::max (inR, 1.0)), outRate (std::max (outR, 1.0)), ratio (inRate / outRate),
      bps (std::clamp (outBps, 1, 32)), frameBits (8 * valueBytes (bps)), mode (m),
      seed0 (seed != 0 ? seed : 1u), rng (seed0)
{
    if (mode == ConvertMode::clean)
    {
        // Cut off just under the output's Nyquist (never above the input's), with
        // 12 zero crossings a side of Kaiser window: ~80 dB of stop band.
        const double fc = std::min (1.0, 1.0 / ratio) * 0.94;
        half = 12.0 / fc;
        const int n = static_cast<int> (std::ceil (half * kRes));
        table.assign (static_cast<size_t> (2 * n + 2), 0.0f);
        const double beta = 8.6, norm = bessel0 (beta);
        for (int i = 0; i <= 2 * n; ++i)
        {
            const double d = static_cast<double> (i - n) / kRes;
            const double u = d / half;
            const double w = std::abs (u) >= 1.0 ? 0.0 : bessel0 (beta * std::sqrt (1.0 - u * u)) / norm;
            const double x = kPi * fc * d;
            const double s = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (x) / x;
            table[static_cast<size_t> (i)] = static_cast<float> (fc * s * w);
        }
    }
}

size_t RecordConverter::outputLength (size_t inFrames, double inRate, double outRate) noexcept
{
    const double r = std::max (inRate, 1.0) / std::max (outRate, 1.0);
    return static_cast<size_t> (std::ceil (static_cast<double> (inFrames) / r - 1.0e-9));
}

double RecordConverter::tpdf() noexcept
{
    // xorshift32; the difference of two uniforms in [0,1) is triangular on (-1,1).
    auto u = [this]
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return static_cast<double> (rng) / 4294967296.0;
    };
    return u() - u();
}

int32_t RecordConverter::quantise (double x, bool dither) noexcept
{
    const double scale = std::ldexp (1.0, bps - 1);
    const double lo = -scale, hi = scale - 1.0;
    double v = x * scale;
    if (dither) v = std::floor (v + tpdf() + 0.5);   // round(v + TPDF), +-1 LSB
    else        v = std::floor (v);                  // truncation: the low bits are dropped
    v = std::clamp (v, lo, hi);
    return static_cast<int32_t> (static_cast<int64_t> (v) * (int64_t (1) << (frameBits - bps)));
}

void RecordConverter::emit (double l, double r, std::vector<int32_t>& outL, std::vector<int32_t>& outR)
{
    const bool dither = mode == ConvertMode::clean && bps <= 24;
    outL.push_back (quantise (l, dither));
    outR.push_back (quantise (r, dither));
}

void RecordConverter::resetStream()
{
    next = 0.0; outIdx = 0; consumed = 0; histBase = 0;
    histL.clear(); histR.clear();
    rng = seed0;
}

void RecordConverter::runClean (std::vector<int32_t>& outL, std::vector<int32_t>& outR, bool final)
{
    const size_t end = histBase + histL.size();       // one past the last input we hold
    const size_t total = consumed;
    const int nHalf = static_cast<int>((table.size() - 2) / 2);

    for (;;)
    {
        // The window must be fully held (the last input it reaches is below `end`),
        // unless this is the end of the stream, where what is missing reads as zero.
        const double reachHi = std::floor (next + half);
        if (! final && reachHi >= static_cast<double>(end)) break;
        if (final && next >= static_cast<double>(total)) break;

        const long lo = static_cast<long>(std::ceil (next - half));
        const long hi = static_cast<long>(reachHi);
        double accL = 0.0, accR = 0.0;
        for (long i = lo; i <= hi; ++i)
        {
            if (i < static_cast<long>(histBase) || i >= static_cast<long>(end)) continue;   // before the start / past the end: zero
            const double pos = (static_cast<double>(i) - next) * kRes + nHalf;   // table index (fractional)
            const long p0 = static_cast<long>(std::floor (pos));
            if (p0 < 0 || p0 + 1 >= static_cast<long>(table.size())) continue;
            const double f = pos - static_cast<double>(p0);
            const double h = table[static_cast<size_t>(p0)] * (1.0 - f) + table[static_cast<size_t>(p0 + 1)] * f;
            accL += h * histL[static_cast<size_t>(i) - histBase];
            accR += h * histR[static_cast<size_t>(i) - histBase];
        }
        emit (accL, accR, outL, outR);
        next = static_cast<double>(++outIdx) * ratio;
    }

    // Forget input no later output can reach.
    const double keepFrom = std::floor (next - half) - 1.0;
    if (keepFrom > static_cast<double>(histBase))
    {
        const size_t drop = std::min (histL.size(), static_cast<size_t>(keepFrom) - histBase);
        histL.erase (histL.begin(), histL.begin() + static_cast<std::ptrdiff_t>(drop));
        histR.erase (histR.begin(), histR.begin() + static_cast<std::ptrdiff_t>(drop));
        histBase += drop;
    }
}

void RecordConverter::process (const float* l, const float* r, size_t n,
                               std::vector<int32_t>& outL, std::vector<int32_t>& outR)
{
    if (mode == ConvertMode::raw)
    {
        // Zero-order hold: output k is input floor(k * ratio), no filter.
        while (next < static_cast<double>(consumed + n))
        {
            const size_t idx = static_cast<size_t>(std::floor (next)) - consumed;
            emit (l[idx], r[idx], outL, outR);
            next = static_cast<double>(++outIdx) * ratio;
        }
        consumed += n;
        return;
    }

    histL.insert (histL.end(), l, l + n);
    histR.insert (histR.end(), r, r + n);
    consumed += n;
    runClean (outL, outR, false);
}

void RecordConverter::flush (std::vector<int32_t>& outL, std::vector<int32_t>& outR)
{
    if (mode == ConvertMode::clean) runClean (outL, outR, true);
    resetStream();
}

} // namespace se
