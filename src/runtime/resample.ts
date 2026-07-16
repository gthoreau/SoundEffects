/**
 * The host resamplers — `ModResampleSoundRate` / `ModResampleSoundBits`.
 * `[BINARY: disassembly/Host-Resamplers.md]`, ported from `CODE 4 @0x42be` and
 * `CODE 4 @0x3d44`.
 *
 * These are real host DSP, not the OS SoundConverter, and they are *the* reason
 * Resample Rate / Upsample Bits / Downsample Bits contain no conversion code of
 * their own. Two headline facts, both audible:
 *
 *   - **Rate conversion is drop-sample / zero-order-hold, not interpolation.**
 *     Sub-sample precision is carried in a float accumulator and then thrown
 *     away at read time. The stairstep aliasing is the character.
 *   - **Bit conversion is plain truncation / zero-extension.** No rounding, no
 *     dither, ever — which is exactly why the Downsample Bits *module* bothers
 *     to inject noise before calling this.
 */

import type { SoundDocument } from "../document.js";
import { frameSizeFor, silentByte } from "../samples.js";
import { fixedToDouble, type Fixed } from "../units.js";
import { kModNoError, type ModResult } from "./results.js";

/**
 * `ModResampleSoundRate(modData, newRate)` — whole document, per channel.
 * `[BINARY: CODE 4 @0x42be]`
 *
 * Zero-order hold: step an extended-float position through the source by
 * `oldFrameSize · oldRate/newRate` bytes per output sample, **truncate toward
 * zero**, and copy that one source frame verbatim. Upsampling duplicates
 * samples; downsampling drops them. No blend of neighbours anywhere.
 *
 * The original walks backward so in-place reads never collide with earlier
 * writes (the same trick as Echo/Delay/Flanger). We allocate a fresh buffer
 * instead — the direction is unobservable once the source is immutable, and the
 * output is identical. If this ever grows an in-place fast path, the backward
 * walk becomes load-bearing again.
 */
export function modResampleSoundRate(doc: SoundDocument, newRate: Fixed): ModResult {
  // Clamped to >= 0x10000 (1.0 Hz) before anything else. No `>>> 0` here: the
  // rate's integer part is no longer capped at 16 bits (see `units.ts`), and
  // wrapping it to 32 would silently ruin any target above 65.5 kHz.
  const rate = Math.max(newRate, 0x10000);
  const oldFrameSize = frameSizeFor(doc.bps);
  const oldRateD = fixedToDouble(doc.rate);
  const newRateD = fixedToDouble(rate);

  // The source advance, in BYTES, per output sample.
  const step = (oldFrameSize * oldRateD) / newRateD;

  for (const ch of doc.channels) {
    const src = ch.data;
    const oldSize = src.length;
    // New channel byte-size = round(oldSize * newRate / oldRate).
    let newSize = Math.round((oldSize * newRateD) / oldRateD);
    newSize -= newSize % oldFrameSize; // whole frames only
    const dst = new Uint8Array(newSize);

    let pos = 0;
    for (let out = 0; out + oldFrameSize <= newSize; out += oldFrameSize) {
      // FTINTX: round toward zero to a byte offset, then frame-align the read.
      let idx = Math.trunc(pos);
      idx -= idx % oldFrameSize;
      if (idx + oldFrameSize <= oldSize) {
        for (let b = 0; b < oldFrameSize; b++) dst[out + b] = src[idx + b]!;
      }
      pos += step;
    }
    ch.data = dst;
  }

  doc.rate = rate;
  doc.selSt = 0;
  doc.selEnd = 0;
  return kModNoError;
}

/**
 * `ModResampleSoundBits(modData, newBps)` — per channel.
 * `[BINARY: CODE 4 @0x3d44]`
 *
 * Three cases, all of them straight bit surgery with no rounding:
 *   1. **Narrowing bytes** (16→8): keep the top `newFrameSize` bytes, drop the
 *      low ones. The 8-bit destination re-applies `XOR 0x80`.
 *   2. **Same bytes, fewer bits** (16→12): AND off the low `oldBps−newBps` bits
 *      in place; the buffer size is unchanged.
 *   3. **Widening** (8→16): grow, shift each sample left, zero-pad the new low
 *      bytes. The 8-bit source un-offsets via `XOR 0x80` first.
 */
export function modResampleSoundBits(doc: SoundDocument, newBps: number): ModResult {
  const oldBps = doc.bps;
  const oldFrameSize = frameSizeFor(oldBps);
  const newFrameSize = frameSizeFor(newBps);

  if (newFrameSize < oldFrameSize) {
    // Case 1 — narrowing the byte count: keep the most-significant bytes.
    for (const ch of doc.channels) {
      const src = ch.data;
      const frames = Math.floor(src.length / oldFrameSize);
      const dst = new Uint8Array(frames * newFrameSize);
      for (let f = 0; f < frames; f++) {
        const s = f * oldFrameSize;
        const d = f * newFrameSize;
        for (let b = 0; b < newFrameSize; b++) dst[d + b] = src[s + b]!;
        // An 8-bit destination is offset-binary; the wide source was not.
        if (newFrameSize === 1) dst[d] = (dst[d]! ^ 0x80) & 0xff;
      }
      ch.data = dst;
    }
  } else if (newFrameSize === oldFrameSize && newBps < oldBps) {
    // Case 2 — same byte count, fewer bits: mask off the low bits in place.
    const drop = oldBps - newBps;
    for (const ch of doc.channels) {
      const buf = ch.data;
      const frames = Math.floor(buf.length / oldFrameSize);
      for (let f = 0; f < frames; f++) {
        const off = f * oldFrameSize;
        // Build the mask across the frame's bytes, low bits cleared.
        let remaining = drop;
        for (let b = oldFrameSize - 1; b >= 0 && remaining > 0; b--) {
          const bitsHere = Math.min(8, remaining);
          buf[off + b] = (buf[off + b]! & (0xff << bitsHere)) & 0xff;
          remaining -= bitsHere;
        }
      }
    }
  } else if (newFrameSize > oldFrameSize) {
    // Case 3 — widening: shift left, zero-pad the new low bytes.
    for (const ch of doc.channels) {
      const src = ch.data;
      const frames = Math.floor(src.length / oldFrameSize);
      const dst = new Uint8Array(frames * newFrameSize);
      for (let f = 0; f < frames; f++) {
        const s = f * oldFrameSize;
        const d = f * newFrameSize;
        for (let b = 0; b < oldFrameSize; b++) {
          let byte = src[s + b]!;
          if (oldFrameSize === 1) byte = (byte ^ 0x80) & 0xff; // un-offset first
          dst[d + b] = byte;
        }
        // Low bytes stay zero.
      }
      ch.data = dst;
    }
  }
  // newFrameSize == oldFrameSize && newBps >= oldBps: nothing to do.

  doc.bps = newBps;
  // A frame-size change invalidates byte offsets; the app resets the selection.
  doc.selSt = 0;
  doc.selEnd = 0;
  return kModNoError;
}

/** Fill a freshly grown tail with the depth's silent byte. Used by Echo/Delay. */
export function silenceRange(
  data: Uint8Array,
  start: number,
  end: number,
  bps: number,
): void {
  data.fill(silentByte(bps), start, end);
}
