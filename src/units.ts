/**
 * The unit system — SPEC §2.3, `[SOURCE: ModUnits.h]` for the codes,
 * `[SOURCE: ModUnitConversion.c]` for the arithmetic shape, and
 * `[BINARY: disassembly/Unit-Conversion.md]` for the constants and the
 * six-way dispatch.
 *
 * Durations are *always stored in bytes*; the unit is a display choice.
 */

import { frameSizeFor } from "./samples.js";

/** `[SOURCE: ModUnits.h]` */
export const Unit = {
  samples: 0,
  bytes: 1,
  seconds: 2,
  milliseconds: 3,
  meters: 4,
  feet: 5,
} as const;

export type UnitCode = (typeof Unit)[keyof typeof Unit];

/**
 * Speed of sound as the app stores it: two independently-rounded int16s.
 *
 * 344 m/s and 1128 ft/s disagree by ~0.05% (344 m/s is really 1128.6 ft/s), and
 * that disagreement is authentic — do not derive one from the other.
 * `[BINARY: Unit-Conversion.md]`
 */
export const SPEED_OF_SOUND_METERS = 344;
export const SPEED_OF_SOUND_FEET = 1128;
const MS_PER_SECOND = 1000;

/**
 * A fixed-point sample rate: **Hz × 65536**. `[SOURCE: Glue.h]`, widened.
 *
 * ## A deliberate divergence, and exactly how far it goes
 *
 * The original is a `Fixed` — 16.16 in an *unsigned 32-bit* container (the SDK's
 * own `MyUnsignedFix2Double(modInfo->rate, …)` settles the sign; a signed Fixed
 * would cap at 32767 Hz and the app records at 44.1 kHz). That container tops out
 * at **65535.99998 Hz**, which had two user-visible consequences: Change Pitch on
 * a 44.1 kHz sample wrapped 88.2 kHz round to 22664 Hz — you pressed "up an
 * octave" and it played *lower* — and 96/192 kHz files could not be imported at
 * all without a lossy downsample.
 *
 * We keep the **16 fractional bits** and drop the **16-bit integer cap**. The
 * value is still `Math.round(hz * 65536)`; it simply is not truncated to 32 bits.
 *
 * ### Why this is a small divergence rather than a large one
 *
 * The character of a Fixed rate is its **quantisation**, not its overflow. Every
 * commit still rounds to 1/65536 Hz, so Change Pitch still loses a little, twelve
 * semitone steps still drift from one octave at some rates and not others, and
 * Robotize's `trunc(190 · rate/22254.5454)` still snaps to 189 a tick below the
 * native rate. All of that is preserved and still tested.
 *
 * What goes is the **wrap** — and the wrap was never a recovered behaviour. §14
 * item 23 recorded it honestly: "whether the shipped Keyboard clamped or refused
 * first is unverified". The app never met a rate it couldn't hold, so Ricci never
 * had to decide. We were not reproducing an observed quirk; we were reproducing
 * *our own choice of container*. Given two unverified behaviours, the one that
 * lets people open their files wins.
 *
 * ### What it costs
 *
 * `ModParamsRec.rate` is documented as `Fixed` in §7.3 `[SOURCE: Glue.h]`, so a
 * module compiled against the real ABI would read 32 bits and see garbage above
 * 65.5 kHz. That matters for §12b's firmware target, which should either keep the
 * 32-bit form (and live at ≤48 kHz, where it fits comfortably) or widen to match.
 * It does not matter for this port, which loads no 68k code.
 *
 * ### Exactness
 *
 * A JS number holds integers exactly to 2^53, so the rate is exact to
 * `MAX_FIXED_RATE_HZ` = 2^37 ≈ 137 GHz. Verified: over the whole original 2^32
 * range the widened arithmetic and the old 32-bit arithmetic agree **exactly**, so
 * no existing Fixed value changed meaning — including Robotize's `0x56EE8BA3`.
 */
export type Fixed = number;

/**
 * The largest rate this representation holds exactly: 2^53 / 65536 = 2^37 Hz.
 *
 * Effectively unbounded for audio — but real, so `documentFromChannels` still
 * guards rather than letting a rate silently lose precision.
 */
export const MAX_FIXED_RATE_HZ = Math.pow(2, 37);

/**
 * `[BINARY: the CODE 7 @0x11b6 helper — integer half + fractional half / 65536]`
 *
 * The original sums `(f >> 16)` and `(f & 0xFFFF)/65536` because a 68k register is
 * 32 bits wide. `f / 65536` is the same number for every value that fits in 32
 * bits (proven by exhaustive comparison) and keeps working above it.
 */
export function fixedToDouble(f: Fixed): number {
  return f / 65536;
}

/** Hz → Fixed. Still rounds to 1/65536 — the quantisation is the character. */
export function doubleToFixed(d: number): Fixed {
  return Math.round(d * 65536);
}

/** The rate the Mac sampled at by default: 22254.5454 Hz, exact as Fixed. */
export const RATE_22K = doubleToFixed(22254.5454);
export const RATE_44K1 = doubleToFixed(44100);

/** Multiplier applied to a duration in seconds to get the display unit. */
function secondsScale(unit: UnitCode): number | null {
  switch (unit) {
    case Unit.seconds:
      return 1;
    case Unit.milliseconds:
      return MS_PER_SECOND;
    case Unit.meters:
      return SPEED_OF_SOUND_METERS;
    case Unit.feet:
      return SPEED_OF_SOUND_FEET;
    default:
      return null;
  }
}

/**
 * bytes → the value shown in the units popup. `[BINARY: CODE 7 @0x4544]`
 *
 * Faithful detail from `ConvBytesToMilliseconds`: the byte→sample division is
 * *integer* and happens before the float math (`(double)(value/frameSize)`), so
 * a byte count that isn't frame-aligned truncates down before conversion.
 * Codes outside 0..5 are a no-op in the original; we surface that as the byte
 * count unchanged.
 */
export function bytesToUnit(
  bytes: number,
  unit: UnitCode,
  rate: Fixed,
  bps: number,
): number {
  const frameSize = frameSizeFor(bps);
  if (unit === Unit.bytes) return bytes;
  const samples = Math.floor(bytes / frameSize);
  if (unit === Unit.samples) return samples;
  const scale = secondsScale(unit);
  if (scale === null) return bytes; // <0 or >=6: returns unchanged
  return (samples / fixedToDouble(rate)) * scale;
}

/**
 * The value typed into a unit-dependent field → bytes. `[BINARY: CODE 7 @0x47ac]`
 *
 * Mirrors `ConvMillisecondsToBytes`: compute in floating point, truncate to an
 * unsigned long, *then* snap down to a frame boundary. Both steps matter — the
 * truncation is not a rounding.
 */
export function unitToBytes(
  value: number,
  unit: UnitCode,
  rate: Fixed,
  bps: number,
): number {
  const frameSize = frameSizeFor(bps);
  if (unit === Unit.bytes) return Math.trunc(value);
  if (unit === Unit.samples) return Math.trunc(value) * frameSize;
  const scale = secondsScale(unit);
  if (scale === null) return Math.trunc(value);
  const bytes = Math.trunc(((value / scale) * fixedToDouble(rate)) * frameSize);
  return bytes - (bytes % frameSize);
}
