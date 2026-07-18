#include "Mixer.h"

#include <algorithm>
#include <cmath>

namespace se {

double volumeScalar (int step) noexcept
{
    const int s = std::clamp (step, 0, 7);
    return VOLUME_STEPS[static_cast<size_t> (s)];
}

bool isAudible (const TrackMonitor& monitor, int track) noexcept
{
    if (monitor.muted.count (track) > 0) return false;
    if (! monitor.soloed.empty())        return monitor.soloed.count (track) > 0;
    return true;
}

/** The float divisor for a bit depth: 2^(min(frameSize,4)*8 - 1). */
static double floatDivisor (int bps) noexcept
{
    const int bits = std::min (frameSizeFor (bps), 4) * 8;
    return std::pow (2.0, bits - 1);
}

static float clampFloat (double v) noexcept
{
    if (v < -1.0) return -1.0f;
    if (v >  1.0) return  1.0f;
    return static_cast<float> (v);
}

StereoFrame mixFrame (const SoundDocument& doc, size_t pos, const MixOptions& opts) noexcept
{
    const int    frameSize = doc.frameSize();
    const double divisor   = floatDivisor (doc.bps);
    const double master    = volumeScalar (opts.volume);

    double left = 0.0, right = 0.0;

    for (int t = 0; t < doc.numTracks(); ++t)
    {
        if (opts.monitor != nullptr && ! isAudible (*opts.monitor, t)) continue;

        const Track& track = doc.tracks[static_cast<size_t> (t)];

        // Past this track's end? It simply contributes nothing.
        if (pos + static_cast<size_t> (frameSize) > track.data.size()) continue;

        // 8.8 fixed: 0x0100 = 1.0, >1.0 overdrives, <=0 is silent.
        const double vol = track.volume / static_cast<double> (0x0100);
        if (vol <= 0.0) continue;

        const double s = (getSample (track.data.data(), pos, doc.bps) / divisor) * vol;
        if (track.outLeft)  left  += s;
        if (track.outRight) right += s;
    }

    left  *= master;
    right *= master;

    if (opts.forceMono)
    {
        const double m = left + right;
        left = right = m;
    }

    return { clampFloat (left), clampFloat (right) };
}

} // namespace se
