// The two random number generators — and they are genuinely two.
//
// Ported from the TS reference `src/runtime/rng.ts`.
//
// The Ricci modules (Noise, Dither, Downsample Bits) call the Toolbox `Random`
// trap; the Norris modules (Granular Synthesis, Sample Hose) statically link the
// ANSI-C `rand()` LCG. `Dither-IIR-Filters.md` calls this out explicitly — "the
// same RNG as Noise, *not* the ANSI-C LCG the Norris modules use". Do NOT collapse
// them into one: it would silently change what half the catalogue sounds like.
//
// Their confidence differs too, which is why they stay separate types rather than
// one seeded generator:
//   - the Norris LCG is [BINARY] and byte-exact — grain jitter is exactly
//     reproducible, so the port can be verified against the original;
//   - the Toolbox trap is [DOC] — `Noise.md` says outright that the exact RNG
//     doesn't matter, since any decent PRNG reproduces the character.

#pragma once

#include <cstdint>

namespace se {

/**
 * The Macintosh Toolbox `Random()` trap ($A861). [DOC]
 *
 * Park–Miller "minimal standard" Lehmer generator with the multiplier 16807 that
 * QuickDraw's Random is documented to use, driving the same `qd.randSeed` global.
 * NOT byte-verified against the ROM, and `Noise.md` says it need not be. If
 * sample-exact Noise ever matters, this is the one place to fix.
 *
 * Modules do not seed it — whatever state the app is in at the time is what they
 * get. `ModInitRandSeed` exists for modules that want a fresh one; Noise never
 * calls it.
 */
class ToolboxRandom
{
public:
    explicit ToolboxRandom (uint32_t seed = 1) noexcept : state (seed == 0 ? 1 : seed) {}

    /** ModInitRandSeed() — reseed, as the host service does. */
    void initSeed (uint32_t seed) noexcept { state = seed == 0 ? 1 : seed; }

    /** One Random() call: a signed 16-bit value. Callers scale by /32767, so the
        range is what matters, not the bit pattern. */
    int32_t next() noexcept
    {
        // state < 2^31, so the product fits in 64 bits exactly — the reference
        // relies on the same exactness in a double.
        state = static_cast<uint32_t> ((static_cast<uint64_t> (state) * 16807u) % 2147483647u);
        return static_cast<int16_t> (state & 0xffff);
    }

private:
    uint32_t state;
};

/**
 * The textbook ANSI-C `rand()` LCG, statically linked into the Norris modules.
 * [BINARY: Granular-Synthesis.md] — byte-exact.
 *
 *     seed = seed·0x41C64E6D + 0x3039;  return (seed>>16) & 0x7FFF
 */
class AnsiCRandom
{
public:
    static constexpr int32_t kRandMax = 0x7fff;

    explicit AnsiCRandom (uint32_t seed = 1) noexcept : state (seed) {}

    /** rand() — 0..0x7FFF. */
    int32_t next() noexcept
    {
        state = state * 0x41c64e6du + 0x3039u;   // wraps at 32 bits, as intended
        return static_cast<int32_t> ((state >> 16) & 0x7fffu);
    }

private:
    uint32_t state;
};

} // namespace se
