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

static int64_t modulo (int64_t a, int64_t m) noexcept
{
    const int64_t r = a % m;
    return r < 0 ? r + m : r;
}

bool readSpan (const PlayContext& ctx, int track, size_t lenFrames, size_t fs,
               size_t& lo, size_t& S, bool& wrap) noexcept
{
    lo = 0; S = 0; wrap = true;
    switch (ctx.mode)
    {
        case LoopMode::once:
            lo = ctx.selSt / fs;
            S  = ctx.selEnd > ctx.selSt ? (ctx.selEnd - ctx.selSt) / fs : 0;
            wrap = false;
            break;

        case LoopMode::all:
            S = lenFrames;
            break;

        case LoopMode::selection:
            if (track < ctx.firstSelTrack - 1 || track > ctx.lastSelTrack - 1) return false;
            S = ctx.selEnd > ctx.selSt ? (ctx.selEnd - ctx.selSt) / fs : 0;
            if (S == 0) S = lenFrames;          // an empty region loops the track
            else        lo = ctx.selSt / fs;
            break;
    }
    return S > 0;
}

TrackRead readFor (const PlayContext& ctx, int track, size_t lenFrames, size_t fs, double pos) noexcept
{
    TrackRead r;
    size_t lo = 0, S = 0;
    bool wrap = true;
    if (! readSpan (ctx, track, lenFrames, fs, lo, S, wrap)) return r;

    const double ph = ctx.phase[static_cast<size_t> (std::min (track, kPhaseTracks - 1))];
    const int64_t u = static_cast<int64_t> (std::floor ((pos + ph) / static_cast<double> (fs)));
    const int64_t s = static_cast<int64_t> (S);

    int64_t i = 0;
    switch (ctx.dir)
    {
        case PlayDirection::forward:  i = wrap ? modulo (u, s) : u;                     r.up = true;  break;
        case PlayDirection::backward: i = s - 1 - (wrap ? modulo (u, s) : u);           r.up = false; break;
        case PlayDirection::pingPong:
        {
            const int64_t v = modulo (u, 2 * s);
            r.up = v < s;
            i = r.up ? v : 2 * s - 1 - v;
            break;
        }
    }
    if (i < 0 || i >= s) return r;              // once: past either end of the span

    r.inSpan = true;
    r.frame = lo + static_cast<size_t> (i);
    r.lo = lo; r.span = S; r.wrap = wrap;
    r.present = true;
    return r;
}

TrackRead trackRead (const SoundDocument& doc, int track, double pos, const PlayContext& ctx) noexcept
{
    const size_t fs = static_cast<size_t> (doc.frameSize());
    const size_t bytes = doc.tracks[static_cast<size_t> (track)].data.size();
    const size_t lenFrames = bytes / fs;
    TrackRead r = readFor (ctx, track, lenFrames, fs, pos);
    r.present = r.inSpan && r.frame < lenFrames;
    return r;
}

double phaseToRead (PlayDirection dir, size_t span, size_t frameInSpan, bool up,
                    double pos, size_t fs, int64_t* uFrames) noexcept
{
    const int64_t s = static_cast<int64_t> (span), i = static_cast<int64_t> (frameInSpan);
    int64_t u = i;
    switch (dir)
    {
        case PlayDirection::forward:  u = i; break;
        case PlayDirection::backward: u = s - 1 - i; break;
        case PlayDirection::pingPong: u = up ? i : 2 * s - 1 - i; break;
    }
    if (uFrames != nullptr) *uFrames = u;
    // A quarter frame of margin: pos + phase must floor back to `u` however the
    // large pos rounds.
    return (static_cast<double> (u) + 0.25) * static_cast<double> (fs) - pos;
}

StereoFrame mixFrame (const SoundDocument& doc, size_t pos,
                      const PlayContext& ctx, const MixOptions& opts) noexcept
{
    return mixFrameAt (doc, static_cast<double> (pos), ctx, opts);
}

StereoFrame mixFrameAt (const SoundDocument& doc, double pos,
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
        const TrackRead tr = trackRead (doc, t, pos, ctx);
        if (! tr.present) continue;

        const size_t rp = tr.frame * fs;
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
