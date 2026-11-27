// Declick — the one rule of 0.6.0 step 3: no playback event may jump the output.
//
// Every start, stop, pause, resume, loop wrap, keyboard strike and release and gain
// switch goes through a ramp or a crossfade of this one length. `[NATIVE]`: the
// 1995 app cut where it was told to.

#pragma once

#include <algorithm>
#include <cmath>

namespace se {

/** The shared ramp / crossfade length. To be tuned by ear. */
constexpr double kDeclickMs = 5.0;

/** That length in output frames; 0 when `ms` is 0 (ramps off, for the tests that
    read exact samples back). */
inline int declickFrames (double deviceRate, double ms) noexcept
{
    return ms <= 0.0 ? 0 : std::max (1, static_cast<int> (std::lround (deviceRate * ms * 0.001)));
}

/** A gain that moves in a straight line to wherever it is aimed, a block at a
    time: at(i) is the value for the block's sample i, advance() ends the block.
    Audio thread. */
struct Ramp
{
    float current = 1.0f, target = 1.0f, step = 0.0f;
    int   left = 0;              // samples still to travel

    void jump (float v) noexcept { current = target = v; left = 0; }
    /** Head for `v` over `frames` samples (0 = at once); a new aim starts from
        wherever the last one got to. */
    void aim (float v, int frames) noexcept
    {
        if (! (v < target) && ! (v > target)) return;
        if (frames <= 0) { jump (v); return; }
        target = v;
        step = (v - current) / static_cast<float> (frames);
        left = frames;
    }
    float at (int i) const noexcept { return i + 1 < left ? current + step * static_cast<float> (i + 1) : target; }
    void advance (int frames) noexcept
    {
        if (frames >= left) jump (target);
        else { current += step * static_cast<float> (frames); left -= frames; }
    }
};

} // namespace se
