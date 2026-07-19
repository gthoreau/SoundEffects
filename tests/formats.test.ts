import { describe, it, expect } from "vitest";
import { SoundDocument } from "../src/document.js";
import { getSample, setSample } from "../src/samples.js";
import { doubleToFixed, fixedToDouble } from "../src/units.js";
import {
  readExtended80,
  writeExtended80,
  pcmToChannels,
  channelsToPcm,
  exportWouldPad,
  floatToIntegerPcm,
  isAiff,
  parseAiff,
  readAiff,
  writeAiff,
  decodeAiff,
  isWav,
  parseWav,
  readWav,
  writeWav,
  decodeWav,
  WaveFormat,
  wavRateLoss,
  detectFormat,
  readSound,
  writeSound,
  inspectImport,
  inspectExport,
  UNKNOWN_FORMAT_ALERT,
  PADS_CHANNELS_ALERT,
  MAX_RATE_HZ,
} from "../src/formats/index.js";

function doc8(channels: number[][], rate = 22254.5454): SoundDocument {
  const d = new SoundDocument({ bps: 8, numChans: channels.length, rate: doubleToFixed(rate) });
  channels.forEach((samples, i) => {
    d.channels[i]!.data = Uint8Array.from(samples.map((s) => (s ^ 0x80) & 0xff));
  });
  d.firstSelCh = 1;
  d.lastSelCh = channels.length;
  return d;
}

const read8 = (d: SoundDocument, ch = 0): number[] =>
  Array.from(d.channels[ch]!.data).map((b) => ((b ^ 0x80) << 24) >> 24);

// ---------------------------------------------------------------------------
// The 80-bit extended — the same SANE format the app computed in
// ---------------------------------------------------------------------------

describe("the IEEE 80-bit extended", () => {
  const roundTrip = (v: number) => {
    const b = new Uint8Array(10);
    writeExtended80(v, b);
    return readExtended80(b);
  };

  it("round-trips the rates that matter", () => {
    for (const r of [8000, 11025, 22050, 44100, 48000, 96000, 192000]) {
      expect(roundTrip(r)).toBeCloseTo(r, 6);
    }
  });

  it("round-trips the Mac's awkward Standard rate exactly enough", () => {
    expect(roundTrip(22254.5454)).toBeCloseTo(22254.5454, 6);
  });

  it("handles zero and negatives", () => {
    expect(roundTrip(0)).toBe(0);
    expect(roundTrip(-44100)).toBeCloseTo(-44100, 6);
  });

  it("encodes 44100 to the canonical bytes", () => {
    // 44100 = 1.0771484375 x 2^15 -> exponent 16383+15 = 16398 = 0x400E
    const b = new Uint8Array(10);
    writeExtended80(44100, b);
    expect(b[0]).toBe(0x40);
    expect(b[1]).toBe(0x0e);
    expect(b[2]).toBe(0xac);
    expect(b[3]).toBe(0x44);
  });

  it("is wider than the Fixed we hold rates in, so writing never loses", () => {
    const fixed = doubleToFixed(22254.5454);
    const asDouble = fixedToDouble(fixed);
    expect(roundTrip(asDouble)).toBe(asDouble);
  });
});

// ---------------------------------------------------------------------------
// PCM conversion — the symmetry
// ---------------------------------------------------------------------------

describe("PCM conversion", () => {
  it("takes WAV 8-bit unsigned as-is — it IS offset-binary", () => {
    // 0x80 = zero in both worlds.
    const data = Uint8Array.from([0x80, 0xff, 0x00]);
    const [ch] = pcmToChannels(data, {
      bitsPerSample: 8,
      channels: 1,
      littleEndian: true,
      eightBitUnsigned: true,
    });
    expect(Array.from(ch!)).toEqual([0x80, 0xff, 0x00]); // untouched
  });

  it("flips AIFF 8-bit signed into offset-binary", () => {
    // AIFF signed 0 -> model 0x80.
    const data = Uint8Array.from([0x00, 0x7f, 0x80]);
    const [ch] = pcmToChannels(data, {
      bitsPerSample: 8,
      channels: 1,
      littleEndian: false,
      eightBitUnsigned: false,
    });
    expect(Array.from(ch!)).toEqual([0x80, 0xff, 0x00]);
  });

  it("takes AIFF 16-bit big-endian as-is — it IS the model's layout", () => {
    const data = Uint8Array.from([0x12, 0x34, 0xff, 0xff]);
    const [ch] = pcmToChannels(data, {
      bitsPerSample: 16,
      channels: 1,
      littleEndian: false,
      eightBitUnsigned: false,
    });
    expect(Array.from(ch!)).toEqual([0x12, 0x34, 0xff, 0xff]);
    expect(getSample(ch!, 0, 16)).toBe(0x1234);
  });

  it("byte-swaps WAV 16-bit little-endian", () => {
    const data = Uint8Array.from([0x34, 0x12]); // LE 0x1234
    const [ch] = pcmToChannels(data, {
      bitsPerSample: 16,
      channels: 1,
      littleEndian: true,
      eightBitUnsigned: false,
    });
    expect(getSample(ch!, 0, 16)).toBe(0x1234);
  });

  it("de-interleaves channels", () => {
    // L R L R
    const data = Uint8Array.from([1, 2, 3, 4]);
    const chans = pcmToChannels(data, {
      bitsPerSample: 8,
      channels: 2,
      littleEndian: true,
      eightBitUnsigned: true,
    });
    expect(Array.from(chans[0]!)).toEqual([1, 3]);
    expect(Array.from(chans[1]!)).toEqual([2, 4]);
  });

  it("handles 24-bit natively — the model is bps 1..32", () => {
    const data = Uint8Array.from([0x12, 0x34, 0x56]);
    const [ch] = pcmToChannels(data, {
      bitsPerSample: 24,
      channels: 1,
      littleEndian: false,
      eightBitUnsigned: false,
    });
    expect(getSample(ch!, 0, 24)).toBe(0x123456);
  });

  it("interleaves back on the way out", () => {
    const d = doc8([
      [1, 3],
      [2, 4],
    ]);
    const pcm = channelsToPcm(d, { littleEndian: true, eightBitUnsigned: true });
    expect(Array.from(pcm).map((b) => (b ^ 0x80) & 0xff)).toEqual([1, 2, 3, 4]);
  });
});

// The app's core weirdness meets a format that cannot express it.
describe("unequal channels cannot survive a round trip", () => {
  it("notices when a document would be padded", () => {
    expect(exportWouldPad(doc8([[1, 2, 3, 4]]))).toBe(false); // mono is fine
    expect(exportWouldPad(doc8([[1, 2], [3, 4]]))).toBe(false); // equal is fine
    expect(exportWouldPad(doc8([[1, 2, 3, 4], [3, 4]]))).toBe(true); // drifted
  });

  it("pads the short channel with silence on export", () => {
    const d = doc8([
      [10, 20, 30],
      [40],
    ]);
    const pcm = channelsToPcm(d, { littleEndian: false, eightBitUnsigned: false });
    // Three frames of stereo; channel 2 is silent after its first sample.
    expect(pcm.length).toBe(6);
    const signed = Array.from(pcm).map((b) => (b << 24) >> 24);
    expect(signed).toEqual([10, 40, 20, 0, 30, 0]);
  });

  it("loses the drift for good — a round trip returns aligned channels", () => {
    const d = doc8([
      [10, 20, 30],
      [40],
    ]);
    const back = readAiff(writeAiff(d));
    expect(back.channels[0]!.data.length).toBe(3);
    expect(back.channels[1]!.data.length).toBe(3); // was 1
    expect(exportWouldPad(back)).toBe(false);
  });
});

describe("float to integer — an identity constraint, not a gap", () => {
  it("converts 32-bit float, truncating like everything else here", () => {
    const src = new Uint8Array(8);
    const view = new DataView(src.buffer);
    view.setFloat32(0, 0.5, true);
    view.setFloat32(4, -0.5, true);
    const out = floatToIntegerPcm(src, 32, 16, 1, true);
    expect(getSample(out, 0, 16)).toBe(16384); // 0.5 * 32768
    expect(getSample(out, 2, 16)).toBe(-16384);
  });

  it("clamps out-of-range floats rather than wrapping", () => {
    const src = new Uint8Array(8);
    const view = new DataView(src.buffer);
    view.setFloat32(0, 2.0, true); // hot
    view.setFloat32(4, -2.0, true);
    const out = floatToIntegerPcm(src, 32, 16, 1, true);
    expect(getSample(out, 0, 16)).toBe(32767);
    expect(getSample(out, 2, 16)).toBe(-32768);
  });
});

// ---------------------------------------------------------------------------
// AIFF
// ---------------------------------------------------------------------------

describe("AIFF", () => {
  it("recognises its own files", () => {
    const d = doc8([[1, 2]]);
    const bytes = writeAiff(d);
    expect(isAiff(bytes)).toBe(true);
    expect(isWav(bytes)).toBe(false);
    expect(detectFormat(bytes)).toBe("aiff");
  });

  it("writes a well-formed FORM/COMM/SSND", () => {
    const d = doc8([[1, 2, 3, 4]], 44100);
    const info = parseAiff(writeAiff(d));
    expect(info.channels).toBe(1);
    expect(info.frames).toBe(4);
    expect(info.bitsPerSample).toBe(8);
    expect(info.rateHz).toBeCloseTo(44100, 3);
  });

  it("round-trips 8-bit", () => {
    const d = doc8([[0, 64, -64, 127, -128]]);
    const back = readAiff(writeAiff(d));
    expect(read8(back)).toEqual([0, 64, -64, 127, -128]);
    expect(back.bps).toBe(8);
  });

  it("round-trips 16-bit", () => {
    const d = new SoundDocument({ bps: 16, numChans: 1, rate: doubleToFixed(44100) });
    d.channels[0]!.data = new Uint8Array(6);
    setSample(d.channels[0]!.data, 0, 12345, 16);
    setSample(d.channels[0]!.data, 2, -12345, 16);
    setSample(d.channels[0]!.data, 4, 32767, 16);
    const back = readAiff(writeAiff(d));
    expect(getSample(back.channels[0]!.data, 0, 16)).toBe(12345);
    expect(getSample(back.channels[0]!.data, 2, 16)).toBe(-12345);
    expect(getSample(back.channels[0]!.data, 4, 16)).toBe(32767);
  });

  it("round-trips 24-bit", () => {
    const d = new SoundDocument({ bps: 24, numChans: 1, rate: doubleToFixed(48000) });
    d.channels[0]!.data = new Uint8Array(3);
    setSample(d.channels[0]!.data, 0, 8388607, 24);
    const back = readAiff(writeAiff(d));
    expect(back.bps).toBe(24);
    expect(getSample(back.channels[0]!.data, 0, 24)).toBe(8388607);
  });

  it("round-trips stereo", () => {
    const d = doc8([
      [10, 20],
      [30, 40],
    ]);
    const back = readAiff(writeAiff(d));
    expect(back.numChans).toBe(2);
    expect(read8(back, 0)).toEqual([10, 20]);
    expect(read8(back, 1)).toEqual([30, 40]);
  });

  // The reason §10 calls AIFF canonical.
  it("round-trips the Mac's fractional rate — its 80-bit field has room", () => {
    const d = doc8([[1]], 22254.5454);
    const back = readAiff(writeAiff(d));
    expect(fixedToDouble(back.rate)).toBeCloseTo(22254.5454, 4);
    expect(back.rate).toBe(d.rate); // bit-identical Fixed
  });

  it("refuses AIFC compression rather than half-reading it", () => {
    const d = doc8([[1, 2]]);
    const bytes = writeAiff(d);
    // Forge an AIFC header with a compression type.
    const aifc = Uint8Array.from(bytes);
    aifc[8] = 0x41; aifc[9] = 0x49; aifc[10] = 0x46; aifc[11] = 0x43; // "AIFC"
    // Widen COMM to 22 and append a bogus compression type.
    const grown = new Uint8Array(aifc.length + 4);
    grown.set(aifc.subarray(0, 16), 0);
    grown[19] = 22; // COMM size
    grown.set(aifc.subarray(20, 38), 20);
    grown.set([0x69, 0x6d, 0x61, 0x34], 38); // "ima4"
    grown.set(aifc.subarray(38), 42);
    const info = parseAiff(grown);
    expect(info.compressionType).toBe("ima4");
    expect(info.compressed).toBe(true);
    expect(() => decodeAiff(grown)).toThrow(/ima4/);
  });

  it("rejects a file with no COMM", () => {
    const bogus = new Uint8Array(12);
    bogus.set([0x46, 0x4f, 0x52, 0x4d], 0); // FORM
    bogus.set([0x41, 0x49, 0x46, 0x46], 8); // AIFF
    expect(() => parseAiff(bogus)).toThrow(/COMM/);
  });
});

// ---------------------------------------------------------------------------
// WAV
// ---------------------------------------------------------------------------

describe("WAV", () => {
  it("recognises its own files", () => {
    const bytes = writeWav(doc8([[1, 2]]));
    expect(isWav(bytes)).toBe(true);
    expect(isAiff(bytes)).toBe(false);
    expect(detectFormat(bytes)).toBe("wav");
  });

  it("writes a well-formed RIFF/fmt/data", () => {
    const d = doc8([[1, 2, 3, 4]], 44100);
    const info = parseWav(writeWav(d));
    expect(info.formatTag).toBe(WaveFormat.pcm);
    expect(info.channels).toBe(1);
    expect(info.sampleRate).toBe(44100);
    expect(info.bitsPerSample).toBe(8);
    expect(info.data.length).toBe(4);
  });

  it("round-trips 8-bit", () => {
    const d = doc8([[0, 64, -64, 127, -128]], 44100);
    const back = readWav(writeWav(d));
    expect(read8(back)).toEqual([0, 64, -64, 127, -128]);
  });

  it("round-trips 16-bit through the byte swap", () => {
    const d = new SoundDocument({ bps: 16, numChans: 1, rate: doubleToFixed(44100) });
    d.channels[0]!.data = new Uint8Array(4);
    setSample(d.channels[0]!.data, 0, 12345, 16);
    setSample(d.channels[0]!.data, 2, -32768, 16);
    const back = readWav(writeWav(d));
    expect(getSample(back.channels[0]!.data, 0, 16)).toBe(12345);
    expect(getSample(back.channels[0]!.data, 2, 16)).toBe(-32768);
  });

  it("round-trips 24-bit", () => {
    const d = new SoundDocument({ bps: 24, numChans: 1, rate: doubleToFixed(48000) });
    d.channels[0]!.data = new Uint8Array(3);
    setSample(d.channels[0]!.data, 0, -8388608, 24);
    const back = readWav(writeWav(d));
    expect(getSample(back.channels[0]!.data, 0, 24)).toBe(-8388608);
  });

  it("round-trips stereo", () => {
    const d = doc8([
      [10, 20],
      [30, 40],
    ], 44100);
    const back = readWav(writeWav(d));
    expect(read8(back, 0)).toEqual([10, 20]);
    expect(read8(back, 1)).toEqual([30, 40]);
  });

  // Why §10 makes AIFF canonical and WAV "for convenience".
  it("loses the rate's fraction — fmt has no room for it", () => {
    const d = doc8([[1]], 22254.5454);
    expect(wavRateLoss(d)).toBeGreaterThan(0);
    const back = readWav(writeWav(d));
    // Rounded to the nearest integer Hz: the .5454 is gone for good, and the
    // sound comes back a fraction sharp.
    expect(fixedToDouble(back.rate)).toBe(22255);
    expect(fixedToDouble(back.rate)).not.toBeCloseTo(22254.5454, 3);
  });

  it("loses nothing on an integer rate", () => {
    const d = doc8([[1]], 44100);
    expect(wavRateLoss(d)).toBe(0);
    expect(fixedToDouble(readWav(writeWav(d)).rate)).toBe(44100);
  });

  it("reads WAVE_FORMAT_EXTENSIBLE by digging out the real tag", () => {
    const d = doc8([[10, 20]], 44100);
    const plain = writeWav(d);
    // Rebuild with a 40-byte fmt declaring EXTENSIBLE + a PCM sub-format.
    const out = new Uint8Array(plain.length + 24);
    out.set(plain.subarray(0, 16), 0);
    out[16] = 40; // fmt size
    out[20] = 0xfe; out[21] = 0xff; // WAVE_FORMAT_EXTENSIBLE
    out.set(plain.subarray(22, 36), 22);
    out[36] = 22; out[37] = 0; // cbSize
    out[44] = 0x01; out[45] = 0x00; // sub-format GUID: PCM
    out.set(plain.subarray(36), 60);
    const info = parseWav(out);
    expect(info.formatTag).toBe(WaveFormat.pcm); // resolved from the GUID
    expect(read8(readWav(out))).toEqual([10, 20]);
  });

  it("converts a float WAV to integer at the door", () => {
    // Hand-build a 32-bit float WAV.
    const frames = 2;
    const data = new Uint8Array(frames * 4);
    const dv = new DataView(data.buffer);
    dv.setFloat32(0, 0.5, true);
    dv.setFloat32(4, -0.25, true);
    const out = new Uint8Array(44 + data.length);
    const enc = (o: number, s: string) => { for (let i = 0; i < 4; i++) out[o + i] = s.charCodeAt(i); };
    const u32 = (o: number, v: number) => { out[o] = v & 0xff; out[o+1] = (v>>>8)&0xff; out[o+2] = (v>>>16)&0xff; out[o+3] = (v>>>24)&0xff; };
    const u16 = (o: number, v: number) => { out[o] = v & 0xff; out[o+1] = (v>>>8)&0xff; };
    enc(0, "RIFF"); u32(4, 36 + data.length); enc(8, "WAVE");
    enc(12, "fmt "); u32(16, 16); u16(20, WaveFormat.ieeeFloat); u16(22, 1);
    u32(24, 48000); u32(28, 48000 * 4); u16(32, 4); u16(34, 32);
    enc(36, "data"); u32(40, data.length); out.set(data, 44);

    const decoded = decodeWav(out);
    expect(decoded.bps).toBe(32); // integer now
    expect(getSample(decoded.channels[0]!, 0, 32)).toBe(0.5 * 2147483648);
    expect(getSample(decoded.channels[0]!, 4, 32)).toBe(-0.25 * 2147483648);
  });

  it("rejects a file with no fmt", () => {
    const bogus = new Uint8Array(12);
    bogus.set([0x52, 0x49, 0x46, 0x46], 0); // RIFF
    bogus.set([0x57, 0x41, 0x56, 0x45], 8); // WAVE
    expect(() => parseWav(bogus)).toThrow(/fmt/);
  });
});

// ---------------------------------------------------------------------------
// Policy — the rate ceiling
// ---------------------------------------------------------------------------

/** Build a WAV at an arbitrary rate, for the ceiling tests. */
function wavAt(rate: number, samples: number[], channels = 1): Uint8Array {
  const data = Uint8Array.from(samples.map((s) => (s + 128) & 0xff));
  const out = new Uint8Array(44 + data.length);
  const enc = (o: number, s: string) => { for (let i = 0; i < 4; i++) out[o + i] = s.charCodeAt(i); };
  const u32 = (o: number, v: number) => { out[o] = v & 0xff; out[o+1] = (v>>>8)&0xff; out[o+2] = (v>>>16)&0xff; out[o+3] = (v>>>24)&0xff; };
  const u16 = (o: number, v: number) => { out[o] = v & 0xff; out[o+1] = (v>>>8)&0xff; };
  enc(0, "RIFF"); u32(4, 36 + data.length); enc(8, "WAVE");
  enc(12, "fmt "); u32(16, 16); u16(20, WaveFormat.pcm); u16(22, channels);
  u32(24, rate); u32(28, rate * channels); u16(32, channels); u16(34, 8);
  enc(36, "data"); u32(40, data.length); out.set(data, 44);
  return out;
}

describe("modern sample rates import natively", () => {
  it("has a ceiling far beyond audio, since the rate field was widened", () => {
    expect(MAX_RATE_HZ).toBe(Math.pow(2, 37)); // ~137 GHz
  });

  it("takes every rate people actually use, exactly", () => {
    for (const r of [8000, 22050, 44100, 48000, 88200, 96000, 176400, 192000]) {
      const doc = readSound(wavAt(r, [0, 64]));
      expect(fixedToDouble(doc.rate)).toBe(r);
    }
  });

  it("flags nothing about a 96 kHz file any more", () => {
    expect(inspectImport(wavAt(96000, [0, 64]))).toBeNull();
    expect(inspectImport(wavAt(192000, [0, 64]))).toBeNull();
  });

  it("imports 96 kHz without touching a sample", () => {
    const doc = readSound(wavAt(96000, [0, 20, 40, 60]));
    expect(read8(doc)).toEqual([0, 20, 40, 60]);
    expect(doc.channels[0]!.data.length).toBe(4); // no resample happened
  });

  it("downsamples on request — a preference now, not a rescue", () => {
    const doc = readSound(wavAt(96000, [0, 20, 40, 60, 80, 100, 80, 60]), {
      resampleTo: 48000,
    });
    expect(fixedToDouble(doc.rate)).toBe(48000);
    expect(doc.channels[0]!.data.length).toBe(4);
  });

  // The staging bug that the widening made moot: a 2:1 step must be exactly 2, or
  // the truncating resampler reads every wrong sample.
  it("still resamples through the app's OWN drop-sample resampler", () => {
    const doc = readSound(wavAt(96000, [0, 99, 40, 99, 80, 99, 60, 99]), {
      resampleTo: 48000,
    });
    expect(read8(doc)).toEqual([0, 40, 80, 60]); // the 99s dropped, not blended
  });

  it("handles an awkward ratio", () => {
    const doc = readSound(wavAt(96000, new Array(32).fill(0).map((_, i) => i)), {
      resampleTo: 44100,
    });
    expect(fixedToDouble(doc.rate)).toBe(44100);
    expect(doc.channels[0]!.data.length).toBe(Math.round(32 * (44100 / 96000)));
  });

  it("round-trips a 96 kHz file through AIFF", () => {
    const doc = readSound(wavAt(96000, [0, 20, 40, 60]));
    const back = readSound(writeSound(doc, "aiff"));
    expect(fixedToDouble(back.rate)).toBe(96000);
    expect(read8(back)).toEqual([0, 20, 40, 60]);
  });

  it("refuses to resample TO something past the ceiling", () => {
    expect(() =>
      readSound(wavAt(96000, [0, 64]), { resampleTo: Math.pow(2, 40) }),
    ).toThrow(/ceiling/);
  });
});

describe("import inspection", () => {
  it("says nothing about a file that drops straight in", () => {
    expect(inspectImport(writeWav(doc8([[1, 2]], 44100)))).toBeNull();
  });

  it("rejects an unknown format in the app's voice", () => {
    const junk = Uint8Array.from([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]);
    expect(detectFormat(junk)).toBeNull();
    expect(() => inspectImport(junk)).toThrow(UNKNOWN_FORMAT_ALERT);
    // The first person and the pointer-to-another-tool survive the rewording.
    expect(UNKNOWN_FORMAT_ALERT).toMatch(/^I can only open/);
  });
});

describe("export inspection", () => {
  it("says nothing about a clean export", () => {
    expect(inspectExport(doc8([[1, 2]], 44100), "aiff")).toBeNull();
  });

  it("warns that drifted channels will be padded", () => {
    const d = doc8([[1, 2, 3], [4]]);
    const issues = inspectExport(d, "aiff")!;
    expect(issues.padsChannels).toEqual({ lengths: [3, 1] });
    expect(PADS_CHANNELS_ALERT).toContain("won't bring the difference back");
  });

  it("warns that WAV will round the rate away", () => {
    const d = doc8([[1]], 22254.5454);
    const issues = inspectExport(d, "wav")!;
    expect(issues.rateFractionLost!.stored).toBe(22255);
    // AIFF has no such problem.
    expect(inspectExport(d, "aiff")).toBeNull();
  });
});

describe("readSound / writeSound", () => {
  it("round-trips through either format", () => {
    const d = doc8([[0, 64, -64]], 44100);
    for (const fmt of ["aiff", "wav"] as const) {
      const back = readSound(writeSound(d, fmt));
      expect(read8(back)).toEqual([0, 64, -64]);
      expect(fixedToDouble(back.rate)).toBe(44100);
    }
  });

  it("names the document", () => {
    const bytes = writeAiff(doc8([[1]]));
    expect(readSound(bytes, { name: "drums" }).name).toBe("drums");
  });

  it("selects all channels on import, ready to edit", () => {
    const d = doc8([[1, 2], [3, 4]], 44100);
    const back = readSound(writeSound(d, "aiff"));
    expect(back.firstSelCh).toBe(1);
    expect(back.lastSelCh).toBe(2);
  });
});
