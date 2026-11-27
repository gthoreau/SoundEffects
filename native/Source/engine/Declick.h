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

} // namespace se
