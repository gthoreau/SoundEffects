#include "Mixer.h"

#include <algorithm>
#include <cmath>

namespace se {

/** The float divisor for a bit depth: 2^(min(frameSize,4)*8 - 1). */
static double floatDivisor (int bps) noexcept
{
    const int bits = std::min (frameSizeFor (bps), 4) * 8;
    return static_cast<double> (uint64_t (1) << (bits - 1));   // per track per frame
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
            if (track < ctx.firstSelTrack - 1 || track > ctx.lastSelTrack - 1) return false;
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

    const size_t slot = static_cast<size_t> (std::min (track, kPhaseTracks - 1));
    if (pos < ctx.silentUntil[slot]) return r;  // from a caret: waiting for its next lap
    const double ph = ctx.phase[slot];
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

double trackSampleAt (const SoundDocument& doc, int track, const TrackRead& r, double pos,
                      const PlayContext& ctx, const MixOptions& opts) noexcept
{
    const Track& t = doc.tracks[static_cast<size_t> (track)];
    const size_t fs = static_cast<size_t> (doc.frameSize());
    const size_t len = t.data.size() / fs;
    const double divisor = floatDivisor (doc.bps);
    const auto at = [&] (size_t frame)
    {
        return frame < len ? getSample (t.data.data(), frame * fs, doc.bps) / divisor : 0.0;
    };

    double v = at (r.frame);
    if (opts.ramp <= 0 || opts.srcStep <= 0.0) return v;

    const size_t slot = static_cast<size_t> (std::min (track, kPhaseTracks - 1));
    const double u = (pos + ctx.phase[slot]) / static_cast<double> (fs);
    const double frac = u - std::floor (u);                       // how far into the frame
    const double edge = opts.ramp * opts.srcStep;                  // the ramp, in sound frames

    // The data ends inside the span.
    if (r.frame < len && len < r.lo + r.span)
    {
        const double left = static_cast<double> (len - r.frame) - (r.up ? frac : 1.0 - frac);
        v *= std::clamp ((left - opts.srcStep) / edge, 0.0, 1.0);
    }

    if (r.wrap && ctx.dir != PlayDirection::pingPong && r.span > 1)
    {
        // Sound frames to go before the wrap; a short loop gives the ramp half a lap.
        const size_t i = r.frame - r.lo;
        const double togo = static_cast<double> (r.up ? r.span - i : i + 1) - frac;
        const double x = std::min (edge, 0.5 * static_cast<double> (r.span));
        const double w = std::clamp ((x + opts.srcStep - togo) / x, 0.0, 1.0);
        if (w > 0.0)
        {
            const size_t head = r.up ? r.lo : r.lo + r.span - 1;
            const size_t next = r.up ? head + 1 : head - 1;
            const size_t last = r.up ? r.lo + r.span - 1 : r.lo;
            const double target = std::clamp (2.0 * at (head) - at (next), -1.0, 1.0);
            v += w * (target - at (last));
        }
    }

    // Late from a caret: silent until its next lap, which it fades into.
    if (ctx.silentUntil[slot] > 0.0)
    {
        const double since = (pos - ctx.silentUntil[slot]) / static_cast<double> (fs);
        v *= std::clamp (since / edge, 0.0, 1.0);
    }
    return v;
}

StereoFrame mixFrameAt (const SoundDocument& doc, double pos,
                        const PlayContext& ctx, const MixOptions& opts) noexcept
{
    double left = 0.0, right = 0.0;

    for (int t = 0; t < doc.numTracks(); ++t)
    {
        if (t < 64 && ((opts.excludeMask >> t) & 1u) != 0) continue;

        const Track& track = doc.tracks[static_cast<size_t> (t)];
        const TrackRead tr = trackRead (doc, t, pos, ctx);
        // An absent track still sounds where a wrap is bringing it back.
        if (! tr.inSpan || (! tr.present && opts.ramp <= 0)) continue;

        const double vol = track.volume / static_cast<double> (0x0100);   // 8.8 fixed
        if (vol <= 0.0) continue;

        const double s = trackSampleAt (doc, t, tr, pos, ctx, opts) * vol;
        const bool eased = opts.routeLeft != nullptr && opts.routeRight != nullptr && t < kPhaseTracks;
        left  += s * (eased ? static_cast<double> (opts.routeLeft[t])  : track.outLeft  ? 1.0 : 0.0);
        right += s * (eased ? static_cast<double> (opts.routeRight[t]) : track.outRight ? 1.0 : 0.0);
    }

    return { clampFloat (left), clampFloat (right) };
}

} // namespace se
