/**
 * The IEEE 754 80-bit extended float — AIFF's sample-rate field.
 *
 * A pleasing loop closes here: this is the *same* 80-bit SANE extended the app
 * computed in throughout (Flanger's LFO, Robotize's delay, the Norris tween — all
 * `_FP68K` extended). AIFF is an Apple format, so of course its rate field is a
 * SANE register written to disk. Everywhere else in this port we have been
 * *escaping* extended precision; here we have to speak it.
 *
 * Layout, big-endian: 1 sign bit · 15 exponent bits (bias 16383) · 64 mantissa
 * bits **with an explicit integer bit** (unlike IEEE double's implicit one).
 *
 * Ported from Apple's own `ConvertToIeeeExtended` / `ConvertFromIeeeExtended`,
 * which shipped with the AIFF spec and which every AIFF implementation descends
 * from.
 */

/** `frexp` — x = m · 2^e with 0.5 <= |m| < 1. JS has no built-in. */
function frexp(x: number): [number, number] {
  if (x === 0 || !Number.isFinite(x)) return [x, 0];
  const abs = Math.abs(x);
  let e = Math.max(-1023, Math.floor(Math.log2(abs)) + 1);
  let m = abs * Math.pow(2, -e);
  // log2 rounding can land us just outside [0.5, 1); nudge back in.
  while (m >= 1) {
    m /= 2;
    e++;
  }
  while (m < 0.5) {
    m *= 2;
    e--;
  }
  return [x < 0 ? -m : m, e];
}

/** Read a 10-byte big-endian extended float at `offset`. */
export function readExtended80(bytes: Uint8Array, offset = 0): number {
  const expon = ((bytes[offset]! & 0x7f) << 8) | bytes[offset + 1]!;
  const hiMant =
    ((bytes[offset + 2]! << 24) |
      (bytes[offset + 3]! << 16) |
      (bytes[offset + 4]! << 8) |
      bytes[offset + 5]!) >>> 0;
  const loMant =
    ((bytes[offset + 6]! << 24) |
      (bytes[offset + 7]! << 16) |
      (bytes[offset + 8]! << 8) |
      bytes[offset + 9]!) >>> 0;

  let f: number;
  if (expon === 0 && hiMant === 0 && loMant === 0) {
    f = 0;
  } else if (expon === 0x7fff) {
    f = Infinity;
  } else {
    // The explicit integer bit is why these shifts are -31 and -63 rather than
    // the -32/-64 an implicit-bit format would want.
    const e = expon - 16383;
    f = hiMant * Math.pow(2, e - 31) + loMant * Math.pow(2, e - 63);
  }
  return bytes[offset]! & 0x80 ? -f : f;
}

/** Write a 10-byte big-endian extended float at `offset`. */
export function writeExtended80(value: number, bytes: Uint8Array, offset = 0): void {
  let sign = 0;
  let num = value;
  if (num < 0) {
    sign = 0x8000;
    num = -num;
  }

  let expon: number;
  let hiMant = 0;
  let loMant = 0;

  if (num === 0) {
    expon = 0;
  } else {
    const [fMantInit, e0] = frexp(num);
    let fMant = fMantInit;
    let e = e0;
    if (e > 16384 || !(fMant < 1)) {
      // Infinity or NaN.
      expon = sign | 0x7fff;
    } else {
      e += 16382;
      if (e < 0) {
        // Denormal.
        fMant = fMant * Math.pow(2, e);
        e = 0;
      }
      expon = e | sign;
      fMant = fMant * Math.pow(2, 32);
      let fsMant = Math.floor(fMant);
      hiMant = fsMant >>> 0;
      fMant = (fMant - fsMant) * Math.pow(2, 32);
      fsMant = Math.floor(fMant);
      loMant = fsMant >>> 0;
    }
  }

  bytes[offset] = (expon >> 8) & 0xff;
  bytes[offset + 1] = expon & 0xff;
  bytes[offset + 2] = (hiMant >>> 24) & 0xff;
  bytes[offset + 3] = (hiMant >>> 16) & 0xff;
  bytes[offset + 4] = (hiMant >>> 8) & 0xff;
  bytes[offset + 5] = hiMant & 0xff;
  bytes[offset + 6] = (loMant >>> 24) & 0xff;
  bytes[offset + 7] = (loMant >>> 16) & 0xff;
  bytes[offset + 8] = (loMant >>> 8) & 0xff;
  bytes[offset + 9] = loMant & 0xff;
}
