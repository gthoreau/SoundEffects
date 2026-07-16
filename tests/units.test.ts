import { describe, it, expect } from "vitest";
import {
  Unit,
  bytesToUnit,
  unitToBytes,
  fixedToDouble,
  doubleToFixed,
  RATE_22K,
  SPEED_OF_SOUND_METERS,
  SPEED_OF_SOUND_FEET,
} from "../src/units.js";

const RATE_1K = doubleToFixed(1000); // a rate that makes the arithmetic readable

describe("Fixed 16.16", () => {
  it("round-trips the Mac's awkward standard rate", () => {
    expect(fixedToDouble(RATE_22K)).toBeCloseTo(22254.5454, 4);
  });

  it("carries a 16-bit fraction", () => {
    expect(fixedToDouble(doubleToFixed(0.5))).toBe(0.5);
    expect(fixedToDouble(doubleToFixed(44100))).toBe(44100);
  });
});

describe("bytesToUnit", () => {
  it("returns bytes and samples directly", () => {
    expect(bytesToUnit(100, Unit.bytes, RATE_1K, 16)).toBe(100);
    expect(bytesToUnit(100, Unit.samples, RATE_1K, 16)).toBe(50);
  });

  it("converts seconds and milliseconds through the rate", () => {
    // 1000 samples at 1000 Hz = 1 s; 16-bit so 2000 bytes.
    expect(bytesToUnit(2000, Unit.seconds, RATE_1K, 16)).toBe(1);
    expect(bytesToUnit(2000, Unit.milliseconds, RATE_1K, 16)).toBe(1000);
  });

  it("measures a duration as the distance sound travels in it", () => {
    expect(bytesToUnit(2000, Unit.meters, RATE_1K, 16)).toBe(344);
    expect(bytesToUnit(2000, Unit.feet, RATE_1K, 16)).toBe(1128);
  });

  // ConvBytesToMilliseconds: `(double)(value/frameSize)` — integer division,
  // before the float math. A stray odd byte truncates away.
  it("truncates a non-frame-aligned byte count before converting", () => {
    expect(bytesToUnit(2001, Unit.samples, RATE_1K, 16)).toBe(1000);
    expect(bytesToUnit(2001, Unit.seconds, RATE_1K, 16)).toBe(1);
  });

  it("leaves out-of-range unit codes alone", () => {
    expect(bytesToUnit(123, 9 as never, RATE_1K, 16)).toBe(123);
    expect(bytesToUnit(123, -1 as never, RATE_1K, 16)).toBe(123);
  });
});

describe("unitToBytes", () => {
  it("inverts the simple units", () => {
    expect(unitToBytes(50, Unit.samples, RATE_1K, 16)).toBe(100);
    expect(unitToBytes(100, Unit.bytes, RATE_1K, 16)).toBe(100);
  });

  it("inverts seconds, ms, meters and feet", () => {
    expect(unitToBytes(1, Unit.seconds, RATE_1K, 16)).toBe(2000);
    expect(unitToBytes(1000, Unit.milliseconds, RATE_1K, 16)).toBe(2000);
    expect(unitToBytes(344, Unit.meters, RATE_1K, 16)).toBe(2000);
    expect(unitToBytes(1128, Unit.feet, RATE_1K, 16)).toBe(2000);
  });

  // ConvMillisecondsToBytes: truncate to a long, THEN snap to a frame boundary.
  it("truncates and then frame-snaps", () => {
    // 0.0005 s * 1000 Hz = 0.5 samples = 1 byte at 16-bit -> snapped to 0.
    expect(unitToBytes(0.0005, Unit.seconds, RATE_1K, 16)).toBe(0);
    // 1.5 samples = 3 bytes -> snapped down to 2.
    expect(unitToBytes(0.0015, Unit.seconds, RATE_1K, 16)).toBe(2);
  });
});

describe("the speed-of-sound constants", () => {
  // The headline quirk from Unit-Conversion.md: 344 and 1128 were rounded
  // independently and disagree by ~0.05%. Deriving one from the other loses it.
  it("keeps meters and feet mutually inconsistent, as shipped", () => {
    expect(SPEED_OF_SOUND_METERS).toBe(344);
    expect(SPEED_OF_SOUND_FEET).toBe(1128);
    const derived = SPEED_OF_SOUND_METERS * 3.28084;
    expect(derived).toBeCloseTo(1128.6, 1);
    expect(SPEED_OF_SOUND_FEET).not.toBe(Math.round(derived));
  });

  it("makes the same duration read differently in meters and feet", () => {
    // 344 m is exactly one second, so this round-trip is free of frame
    // truncation and isolates the constants themselves.
    const bytes = unitToBytes(344, Unit.meters, RATE_1K, 8);
    expect(bytes).toBe(1000); // 1 s at 1000 Hz, 1 byte/sample
    const backInFeet = bytesToUnit(bytes, Unit.feet, RATE_1K, 8);
    expect(backInFeet).toBe(1128);
    // A true 344 m is 1128.6 ft. The app is off by 0.6 ft and always will be.
    expect(backInFeet).not.toBeCloseTo(344 * 3.28084, 1);
  });
});
