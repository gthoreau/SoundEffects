// Machinery shared by Michael Norris's modules (Mike's Modules, ©1995).
// [BINARY: disassembly/Granular-Synthesis.md, Sample-Hose-Iterative-Crossfade.md]
// Ported from `src/effects/norris-shared.ts`.
//
// These modules break the house style DELIBERATELY: Ricci's are integer-only and
// truncate; Norris's compute in real floating point — they ship FPU and PowerPC
// builds precisely because of it — and only truncate at the write. That difference
// is authentic and must survive the port. Anything here that looks like it wants
// tidying into integer maths is load-bearing.
//
// Both pieces are BYTE-VERIFIED, and the grain envelope is the best-corroborated
// formula in the whole project: the 68k build, the 68881 build, and the Read Me's
// published equation (recovered as Expressionist source from PICT 1001) all agree.

#pragma once

#include <cmath>

namespace se::modules {

/**
 * The Curvature tween, shared by Granular Synthesis and Sample Hose.
 * [BINARY] — a genuine pow(), confirmed in both modules.
 *
 *     v(n) = ((n/l)^(c/100))·(vf − vi) + vi
 *
 * Curvature 100 = linear, 0 = convex, 200 = concave.
 *
 * Callers truncate the result with FTINTX before using it as a byte count — the
 * same truncate-don't-round habit as everywhere else. That truncation is the
 * CALLER'S, not this function's, so the maths here keeps full precision.
 */
inline double tween (double n, double length, double initial, double final_,
                     double curvature) noexcept
{
    if (length <= 0.0) return initial;
    const double t = n / length;
    // pow(t, c/100). Curvature 100 gives pow(t,1) = t = linear.
    const double shaped = std::pow (t, curvature / 100.0);
    return shaped * (final_ - initial) + initial;
}

/**
 * Granular Synthesis's grain envelope. [BINARY — CONFIRMED three ways]
 *
 *     t = 5x/n − 2.5
 *     P = (1 − e^(−t²))³
 *     y = (1 − P) + P·f
 *
 * which is exactly the linear blend y = E(x)·(1−f) + f between Roads' truncated
 * curve E(x) = 1 − (1 − e^(−(5x/n − 2.5)²))³ at Fade Level 0% and unity at 100%.
 *
 * Here — unlike Smooth — THE DOCUMENTATION WAS TELLING THE TRUTH, including the
 * Read Me's claim that "with a value of 100%, the formula y = 1 is used, and with
 * the value of 0%, the formula below is used". The formula had only ever been
 * published as a picture, which is why it sat unverified for so long.
 *
 * Edges sit at y≈0.0058 and the centre at exactly 1.0 when f=0. The CUBING is what
 * makes it flatter-topped than a Gaussian — Roads' Gaussian/rectangle compromise.
 *
 * @param x offset into the grain, 0..n
 * @param n grain length
 * @param f Fade Level / 100, 0..1
 */
inline double grainEnvelope (double x, double n, double f) noexcept
{
    if (n <= 0.0) return 1.0;
    const double t = (5.0 * x) / n - 2.5;
    const double P = std::pow (1.0 - std::exp (-(t * t)), 3.0);
    return 1.0 - P + P * f;
}

} // namespace se::modules
