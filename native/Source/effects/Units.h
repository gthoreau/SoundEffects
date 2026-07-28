// Units — SPEC §2.3, ported from the TS reference `src/units.ts`
// ([SOURCE: ModUnits.h] for the codes, [BINARY: Unit-Conversion.md] for the
// constants and the six-way dispatch).
//
// Durations are ALWAYS stored in bytes; the unit is a display choice. A module
// never sees anything but bytes — the host converts at the dialog boundary.
//
// The truncations here are load-bearing, not sloppiness: `ConvBytesToMilliseconds`
// does the byte→sample division in INTEGER arithmetic before any float math, and
// `ConvMillisecondsToBytes` truncates to an unsigned long and THEN snaps down to a
// frame boundary. Both steps are observable.

#pragma once

#include "engine/SampleFormat.h"

#include <cmath>
#include <cstdint>

namespace se {

/** [SOURCE: ModUnits.h] */
enum class Unit : int
{
    Samples = 0,
    Bytes = 1,
    Seconds = 2,
    Milliseconds = 3,
    Meters = 4,
    Feet = 5,
};

/**
 * Speed of sound as the app stores it: two independently-rounded int16s.
 *
 * 344 m/s and 1128 ft/s disagree by ~0.05% (344 m/s is really 1128.6 ft/s), and
 * that disagreement is authentic — do not derive one from the other.
 */
inline constexpr int kSpeedOfSoundMeters = 344;
inline constexpr int kSpeedOfSoundFeet = 1128;
inline constexpr double kMsPerSecond = 1000.0;

/**
 * A fixed-point sample rate: Hz × 65536.
 *
 * The engine's SoundDocument keeps `rateHz` as a plain double, but the reference
 * — and the file formats — quantise to 1/65536 Hz, and that quantisation is the
 * character (Robotize's trunc(190 · rate/22254.5454) snaps to 189 a tick below the
 * native rate). Anything deriving a Fixed from the double must ROUND, not
 * truncate: the double came from a Fixed and must land back on the same one.
 */
using Fixed = int64_t;

inline Fixed doubleToFixed (double hz) noexcept
{
    return static_cast<Fixed> (std::llround (hz * 65536.0));
}

inline double fixedToDouble (Fixed f) noexcept
{
    return static_cast<double> (f) / 65536.0;
}

/** The rate the Mac sampled at by default. */
inline const double kRate22K = 22254.5454;

/** Multiplier applied to a duration in seconds to get the display unit, or 0 for
    a unit that is not time-based (or a code outside 0..5, a no-op in the
    original). */
inline double secondsScale (Unit unit) noexcept
{
    switch (unit)
    {
        case Unit::Seconds:      return 1.0;
        case Unit::Milliseconds: return kMsPerSecond;
        case Unit::Meters:       return kSpeedOfSoundMeters;
        case Unit::Feet:         return kSpeedOfSoundFeet;
        // Not time-based: the caller handles these before asking.
        case Unit::Samples:
        case Unit::Bytes:        break;
    }
    return 0.0;   // also the original's no-op answer for a code outside 0..5
}

/**
 * bytes → the value shown in the units popup. [BINARY: CODE 7 @0x4544]
 *
 * The byte→sample division is INTEGER and happens before the float math, so a
 * byte count that isn't frame-aligned truncates down before conversion.
 */
inline double bytesToUnit (size_t bytes, Unit unit, double rateHz, int bps) noexcept
{
    const size_t frameSize = static_cast<size_t> (frameSizeFor (bps));
    if (unit == Unit::Bytes) return static_cast<double> (bytes);

    const size_t samples = bytes / frameSize;          // integer, before any float
    if (unit == Unit::Samples) return static_cast<double> (samples);

    const double scale = secondsScale (unit);
    if (scale == 0.0) return static_cast<double> (bytes);   // codes outside 0..5
    return (static_cast<double> (samples) / rateHz) * scale;
}

/**
 * The value typed into a unit-dependent field → bytes. [BINARY: CODE 7 @0x47ac]
 *
 * Compute in floating point, truncate to an unsigned long, THEN snap down to a
 * frame boundary. The truncation is not a rounding, and both steps matter.
 */
inline size_t unitToBytes (double value, Unit unit, double rateHz, int bps) noexcept
{
    const size_t frameSize = static_cast<size_t> (frameSizeFor (bps));
    if (unit == Unit::Bytes) return static_cast<size_t> (std::trunc (value));
    if (unit == Unit::Samples) return static_cast<size_t> (std::trunc (value)) * frameSize;

    const double scale = secondsScale (unit);
    if (scale == 0.0) return static_cast<size_t> (std::trunc (value));

    const size_t bytes = static_cast<size_t> (std::trunc ((value / scale) * rateHz
                                                          * static_cast<double> (frameSize)));
    return bytes - (bytes % frameSize);
}

} // namespace se
