/**
 * The sample model — SPEC §2.2 / §7.4.
 *
 * Ported from the shipped Developer's Kit source
 * (`SoundEffects Developer's Kit/Interfaces/ModGetSetSamples.{h,c}`), which is
 * `[SOURCE]`: this is the original C, not a reading of the docs. The integer
 * path here is the reason the effects sound the way they do, so it is a
 * transliteration rather than a tidy-up.
 */

/** `frameSize = bps/8 + (bps%8 != 0)` — bytes per sample. `[SOURCE]` */
export function frameSizeFor(bps: number): number {
  return Math.floor(bps / 8) + (bps % 8 !== 0 ? 1 : 0);
}

/**
 * Bytes of the frame that actually carry the value: `min(frameSize, 4)`.
 * A frame wider than a 68k long (bps > 32 is illegal, but bps 25..32 with
 * frameSize 4 is the ceiling) never reads past 4 bytes.
 */
function valueBytes(bps: number): number {
  const size = frameSizeFor(bps);
  return size < 4 ? size : 4;
}

/**
 * Read one sample. `[SOURCE: GetSample8 / GetSample16]`
 *
 * 8 bits and under (frameSize 1) are offset-binary: `signed = stored ^ 0x80`.
 * 9..32 bits are big-endian, loaded into the *high* bytes of a 32-bit word and
 * arithmetic-shifted down, which both sign-extends and right-aligns. Note the
 * consequence called out in §2.2: the value occupies the full range of its byte
 * container, so a 12-bit sound's samples come back as 16-bit values.
 */
export function getSample(data: Uint8Array, offset: number, bps: number): number {
  const n = valueBytes(bps);
  if (n === 1) {
    // GetSample8: ((signed char)(*(signed char *)(x) ^ 0x80))
    return ((data[offset]! ^ 0x80) << 24) >> 24;
  }
  let v = 0;
  for (let b = 0; b < n; b++) v = (v << 8) | data[offset + b]!;
  const shift = (4 - n) * 8;
  return (v << shift) >> shift;
}

/**
 * Write one sample. `[SOURCE: SetSample8 / SetSample16]`
 *
 * The C stores the long's top `numBytes` bytes, which — because a 68k long is
 * big-endian and `GetSample16` right-aligns — means the value's low `numBytes`
 * bytes, big-endian. Bytes of the frame beyond the 4th are left untouched,
 * exactly as the original leaves them.
 */
export function setSample(
  data: Uint8Array,
  offset: number,
  value: number,
  bps: number,
): void {
  const n = valueBytes(bps);
  if (n === 1) {
    // SetSample8: *(signed char *)(x) = (SignedByte)(s) ^ 0x80
    data[offset] = (value ^ 0x80) & 0xff;
    return;
  }
  for (let b = 0; b < n; b++) {
    data[offset + b] = (value >>> ((n - 1 - b) * 8)) & 0xff;
  }
}

/**
 * `ModGetSampleValueLimits(bps, &min, &max)` — the range every effect clamps to.
 *
 * `[DOC/RES]`, not byte-verified. §2.2: "for byte-aligned depths effectively
 * ±(2^(8·nbytes−1))"; the exact behaviour for odd bps (true ±2^11 for 12-bit vs
 * ±2^15) is still `[ASM]`. We take the byte-container reading, which is what
 * §2.2's "a 12-bit sound's samples are read as 16-bit values" implies and what
 * Gain.c's clamp is agnostic to. If the app-level routine is ever disassembled
 * and disagrees for odd bps, this is the one place to fix.
 */
export function sampleValueLimits(bps: number): { min: number; max: number } {
  const bits = valueBytes(bps) * 8;
  // 2**31 is exact in a double; the 68k register would wrap, but effects clamp
  // before they get here.
  const max = Math.pow(2, bits - 1) - 1;
  return { min: -Math.pow(2, bits - 1), max };
}

/** Saturating clamp — the "hard clip" every shipped effect ends with. `[SOURCE: Gain.c]` */
export function clampSample(value: number, min: number, max: number): number {
  if (value < min) return min;
  if (value > max) return max;
  return value;
}

/** The offset-binary / all-zero silent frame test used by Select Silence. `[BINARY]` */
export function isSilentFrame(data: Uint8Array, offset: number, bps: number): boolean {
  const size = frameSizeFor(bps);
  if (size === 1) return data[offset] === 0x80;
  for (let b = 0; b < size; b++) {
    if (data[offset + b] !== 0x00) return false;
  }
  return true;
}

/** The byte value that reads as zero at this depth: 0x80 for 8-bit, 0x00 above. `[BINARY]` */
export function silentByte(bps: number): number {
  return frameSizeFor(bps) === 1 ? 0x80 : 0x00;
}
