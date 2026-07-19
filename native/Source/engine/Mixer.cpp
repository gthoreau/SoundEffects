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

StereoFrame mixFrame (const SoundDocument& doc, size_t pos,
                      const PlayContext& ctx, const MixOptions& opts) noexcept
{
    const int    frameSize = doc.frameSize();
    const size_t fs        = static_cast<size_t> (frameSize);
    const double divisor   = floatDivisor (doc.bps);
    const double master    = volumeScalar (opts.volume);

    double left = 0.0, right = 0.0;

    for (int t = 0; t < doc.numTracks(); ++t)
    {
        if (opts.monitor != nullptr && ! isAudible (*opts.monitor, t)) continue;

        const Track& track = doc.tracks[static_cast<size_t> (t)];
        const size_t len = track.data.size() - (track.data.size() % fs);  // frame-aligned
        if (len == 0) continue;

        // Map the monotonic position to this track's own read position.
        size_t rp = 0;
        bool present = true;
        switch (ctx.mode)
        {
            case LoopMode::once:
                present = pos < len;
                rp = pos;
                break;

            case LoopMode::all:
                rp = pos % len;                       // independent per-track loop
                break;

            case LoopMode::selection:
            {
                if (t < ctx.firstSelTrack - 1 || t > ctx.lastSelTrack - 1) { present = false; break; }
                size_t selLen = ctx.selEnd > ctx.selSt ? ctx.selEnd - ctx.selSt : 0;
                selLen -= selLen % fs;
                if (selLen == 0) { rp = pos % len; break; }
                const size_t phase = (pos >= ctx.selSt ? pos - ctx.selSt : 0) % selLen;
                rp = ctx.selSt + phase;
                present = rp < len;
                break;
            }
        }
        if (! present) continue;

        rp -= rp % fs;
        if (rp + fs > track.data.size()) continue;

        const double vol = track.volume / static_cast<double> (0x0100);   // 8.8 fixed
        if (vol <= 0.0) continue;

        const double s = (getSample (track.data.data(), rp, doc.bps) / divisor) * vol;
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
