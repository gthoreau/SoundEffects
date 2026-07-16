import { describe, it, expect } from "vitest";
import {
  frameSizeFor,
  getSample,
  setSample,
  sampleValueLimits,
  clampSample,
  isSilentFrame,
  silentByte,
} from "../src/samples.js";

describe("frameSizeFor — bps/8 + (bps%8 != 0)", () => {
  it("gives one byte for every depth up to 8", () => {
    for (let bps = 1; bps <= 8; bps++) expect(frameSizeFor(bps)).toBe(1);
  });

  it("rounds odd depths up to whole bytes", () => {
    expect(frameSizeFor(12)).toBe(2); // §2.2's worked example
    expect(frameSizeFor(16)).toBe(2);
    expect(frameSizeFor(17)).toBe(3);
    expect(frameSizeFor(24)).toBe(3);
    expect(frameSizeFor(32)).toBe(4);
  });
});

describe("8-bit samples are offset-binary", () => {
  it("decodes 0x80 as zero and the ends as ±", () => {
    const d = Uint8Array.from([0x80, 0x00, 0xff, 0x81, 0x7f]);
    expect(getSample(d, 0, 8)).toBe(0);
    expect(getSample(d, 1, 8)).toBe(-128);
    expect(getSample(d, 2, 8)).toBe(127);
    expect(getSample(d, 3, 8)).toBe(1);
    expect(getSample(d, 4, 8)).toBe(-1);
  });

  it("round-trips every representable value", () => {
    const d = new Uint8Array(1);
    for (let v = -128; v <= 127; v++) {
      setSample(d, 0, v, 8);
      expect(getSample(d, 0, 8)).toBe(v);
    }
  });

  it("stores what the original stores", () => {
    const d = new Uint8Array(1);
    setSample(d, 0, 0, 8);
    expect(d[0]).toBe(0x80);
    setSample(d, 0, -128, 8);
    expect(d[0]).toBe(0x00);
    setSample(d, 0, 127, 8);
    expect(d[0]).toBe(0xff);
  });

  it("treats sub-8-bit depths as 8-bit containers", () => {
    const d = Uint8Array.from([0x00]);
    expect(getSample(d, 0, 4)).toBe(-128);
  });
});

describe("9–32-bit samples are big-endian, sign-extended", () => {
  it("reads 16-bit big-endian", () => {
    const d = Uint8Array.from([0x00, 0x01, 0xff, 0xff, 0x80, 0x00, 0x7f, 0xff]);
    expect(getSample(d, 0, 16)).toBe(1);
    expect(getSample(d, 2, 16)).toBe(-1);
    expect(getSample(d, 4, 16)).toBe(-32768);
    expect(getSample(d, 6, 16)).toBe(32767);
  });

  it("reads 24-bit big-endian", () => {
    const d = Uint8Array.from([0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x7f, 0xff, 0xff]);
    expect(getSample(d, 0, 24)).toBe(-1);
    expect(getSample(d, 3, 24)).toBe(-8388608);
    expect(getSample(d, 6, 24)).toBe(8388607);
  });

  it("reads 32-bit big-endian", () => {
    const d = Uint8Array.from([0xff, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00]);
    expect(getSample(d, 0, 32)).toBe(-1);
    expect(getSample(d, 4, 32)).toBe(-2147483648);
  });

  it("round-trips 16-bit across the full range", () => {
    const d = new Uint8Array(2);
    for (let v = -32768; v <= 32767; v += 7) {
      setSample(d, 0, v, 16);
      expect(getSample(d, 0, 16)).toBe(v);
    }
  });

  it("round-trips 24-bit", () => {
    const d = new Uint8Array(3);
    for (const v of [-8388608, -1, 0, 1, 8388607, 12345]) {
      setSample(d, 0, v, 24);
      expect(getSample(d, 0, 24)).toBe(v);
    }
  });

  // §2.2: "the value occupies the full range of its byte container — a 12-bit
  // sound's samples are read as 16-bit values."
  it("reads a 12-bit sound as 16-bit values", () => {
    const d = Uint8Array.from([0xff, 0xff]);
    expect(getSample(d, 0, 12)).toBe(-1);
    expect(getSample(d, 0, 12)).toBe(getSample(d, 0, 16));
  });
});

describe("sampleValueLimits", () => {
  it("follows the byte container, not the bit depth", () => {
    expect(sampleValueLimits(8)).toEqual({ min: -128, max: 127 });
    expect(sampleValueLimits(16)).toEqual({ min: -32768, max: 32767 });
    // The [ASM] open question: 12-bit clamps at the 16-bit container's range.
    expect(sampleValueLimits(12)).toEqual({ min: -32768, max: 32767 });
    expect(sampleValueLimits(24)).toEqual({ min: -8388608, max: 8388607 });
  });
});

describe("clampSample", () => {
  it("saturates rather than wrapping", () => {
    const { min, max } = sampleValueLimits(8);
    expect(clampSample(200, min, max)).toBe(127);
    expect(clampSample(-200, min, max)).toBe(-128);
    expect(clampSample(5, min, max)).toBe(5);
  });

  // The Gain.c integer path, end to end: s * gain / 100 then hard clip.
  it("reproduces Amplify's integer rounding", () => {
    const { min, max } = sampleValueLimits(8);
    const gain = 200;
    const amplify = (s: number) => clampSample(Math.trunc((s * gain) / 100), min, max);
    expect(amplify(3)).toBe(6);
    expect(amplify(100)).toBe(127); // clipped, not wrapped
    expect(amplify(-100)).toBe(-128);
  });
});

describe("silence tests", () => {
  it("knows which byte reads as zero", () => {
    expect(silentByte(8)).toBe(0x80);
    expect(silentByte(16)).toBe(0x00);
  });

  it("requires every byte of a wide frame to be zero", () => {
    const d = Uint8Array.from([0x00, 0x00, 0x00, 0x01]);
    expect(isSilentFrame(d, 0, 16)).toBe(true);
    expect(isSilentFrame(d, 2, 16)).toBe(false);
  });
});
