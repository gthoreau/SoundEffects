/**
 * The two random number generators — and they are genuinely two.
 *
 * The Ricci modules (Noise, Dither, Downsample Bits) call the **Toolbox `Random`
 * trap**; the Norris modules (Granular Synthesis, Sample Hose) statically link
 * the **ANSI-C `rand()` LCG**. `Dither-IIR-Filters.md` calls this out explicitly
 * — "the same RNG as Noise, *not* the ANSI-C LCG the Norris modules use". Do not
 * collapse them into one.
 *
 * Their confidence differs too, which is why they are separate classes rather
 * than one seeded generator:
 *   - the Norris LCG is `[BINARY]` and byte-exact — grain jitter is exactly
 *     reproducible, so a port can be verified against the original;
 *   - the Toolbox trap is `[DOC]` — `Noise.md` explicitly says "the exact RNG
 *     doesn't matter for a faithful re-creation (any decent PRNG reproduces the
 *     character)". Sample-exact output is not achievable or wanted here.
 */

/**
 * The Macintosh Toolbox `Random()` trap (`$A861`). `[DOC]`
 *
 * Returns a signed 16-bit value. Callers scale it by `/32767`, so the range is
 * what matters, not the bit pattern.
 *
 * Implemented as the Park–Miller "minimal standard" Lehmer generator with the
 * multiplier 16807 that QuickDraw's `Random` is documented to use, driving the
 * same `qd.randSeed` global. **This is not byte-verified against the ROM** and
 * `Noise.md` says it need not be: any decent PRNG reproduces the character. If
 * sample-exact Noise ever matters, this is the one place to fix.
 *
 * Modules do not seed it — `Noise.md`: "whatever `qd.randSeed` state the app is
 * in at the time is what you get". `GlueRec` exposes `ModInitRandSeed` for
 * modules that want a fresh one; Noise never calls it.
 */
export class ToolboxRandom {
  /** The app-wide `qd.randSeed`. */
  private seed: number;

  constructor(seed = 1) {
    this.seed = seed >>> 0 || 1;
  }

  /** `ModInitRandSeed()` — reseed from the clock, as the host service does. */
  initSeed(seed: number = Date.now() & 0x7fffffff): void {
    this.seed = seed >>> 0 || 1;
  }

  /** One `Random()` call: a signed 16-bit value. */
  next(): number {
    // seed = (seed * 16807) % 2147483647. seed < 2^31 so the product is < 2^45,
    // exact in a double — no need for Schrage's trick here.
    this.seed = (this.seed * 16807) % 2147483647;
    return (this.seed << 16) >> 16;
  }
}

/**
 * The textbook ANSI-C `rand()` LCG, statically linked into the Norris modules.
 * `[BINARY: Granular-Synthesis.md]` — **byte-exact**.
 *
 *     seed = seed·0x41C64E6D + 0x3039;  return (seed>>16) & 0x7FFF
 *
 * Grain jitter is exactly reproducible, which makes Granular Synthesis and
 * Sample Hose testable against the original.
 */
export class AnsiCRandom {
  private seed: number;

  constructor(seed = 1) {
    this.seed = seed >>> 0;
  }

  /** `rand()` — 0..0x7FFF. */
  next(): number {
    // Math.imul keeps the low 32 bits: seed*0x41C64E6D overflows a double's
    // exact-integer range, so a plain multiply would silently lose precision.
    this.seed = (Math.imul(this.seed, 0x41c64e6d) + 0x3039) >>> 0;
    return (this.seed >>> 16) & 0x7fff;
  }

  /** RAND_MAX, for scaling. */
  static readonly RAND_MAX = 0x7fff;
}
