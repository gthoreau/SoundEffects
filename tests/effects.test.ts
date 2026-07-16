import { describe, it, expect } from "vitest";
import { SoundDocument } from "../src/document.js";
import { getSample, setSample } from "../src/samples.js";
import { setSelection } from "../src/selection.js";
import { doubleToFixed, Unit } from "../src/units.js";
import { Host, DoExtend, runEffect, kModCancel, kModNoError } from "../src/runtime/index.js";
import {
  Amplify,
  Smooth,
  Robotize,
  NATIVE_RATE,
  Noise,
  Dither,
  ditherAmplitude,
  DitherMode,
  HighPassIIR,
  LowPassIIR,
  Shelving,
  Echo,
  Delay,
  WeirdTempo,
  segmentBoundaries,
  Invert,
  Reverse,
  Silence,
  DrawLine,
  Normalize,
  LowPassFIR,
  Sum,
  SelectionToChannel,
  buildEffectMenu,
  optionsItemTitle,
  aboutItemTitle,
  findBySignature,
  EFFECTS,
} from "../src/effects/index.js";

/** 8-bit doc from signed sample values. */
function doc8(channels: number[][], rate = 22254.5454): SoundDocument {
  const d = new SoundDocument({ bps: 8, numChans: channels.length, rate: doubleToFixed(rate) });
  channels.forEach((samples, i) => {
    d.channels[i]!.data = Uint8Array.from(samples.map((s) => (s ^ 0x80) & 0xff));
  });
  d.firstSelCh = 1;
  d.lastSelCh = channels.length;
  return d;
}

const read = (d: SoundDocument, ch = 0): number[] =>
  Array.from(d.channels[ch]!.data).map((b) => ((b ^ 0x80) << 24) >> 24);

const host = () => new Host();

describe("Amplify — the SOURCE reference (Gain.c)", () => {
  it("applies integer gain with a truncating divide", () => {
    const d = doc8([[3, -3, 10]]);
    runEffect(Amplify, d, host(), { gain: 150 });
    // 3*150/100 = 4.5 -> 4, not 5. The rounding scar is the point.
    expect(read(d)).toEqual([4, -4, 15]);
  });

  it("clips rather than wrapping", () => {
    const d = doc8([[100, -100]]);
    runEffect(Amplify, d, host(), { gain: 200 });
    expect(read(d)).toEqual([127, -128]);
  });

  it("defaults to gain 200, as the SDK does", () => {
    expect(Amplify.defaults!()).toEqual({ gain: 200 });
  });

  it("clamps gain to a short in settings", () => {
    expect(Amplify.validate!({ gain: 99999 })).toEqual({ gain: 32767 });
  });

  it("processes each channel's own intersection when lengths differ", () => {
    const d = doc8([
      [10, 10, 10, 10],
      [10, 10],
    ]);
    runEffect(Amplify, d, host(), { gain: 200 });
    expect(read(d, 0)).toEqual([20, 20, 20, 20]);
    expect(read(d, 1)).toEqual([20, 20]);
  });

  it("treats an empty selection as the whole channel", () => {
    const d = doc8([[10, 10]]);
    d.selSt = d.selEnd = 0;
    runEffect(Amplify, d, host(), { gain: 200 });
    expect(read(d)).toEqual([20, 20]);
  });
});

describe("Smooth — the docs lied", () => {
  it("averages the two NEIGHBOURS, excluding the centre sample", () => {
    const d = doc8([[0, 100, 0, 0, 0]]);
    runEffect(Smooth, d, host());
    // If it were a 3-point (prev+cur+next)/3, index 1 would be ~33.
    // It is (prev+next)/2 = (0+0)/2 = 0: the 100 erases itself.
    expect(read(d)[1]).toBe(0);
  });

  it("leaves the first and last samples completely untouched", () => {
    const d = doc8([[10, 50, 60, 70, 20]]);
    const before = read(d);
    runEffect(Smooth, d, host());
    const after = read(d);
    expect(after[0]).toBe(before[0]);
    expect(after[4]).toBe(before[4]);
  });

  it("is non-cascading — every average uses original neighbours", () => {
    const d = doc8([[0, 0, 40, 0, 0]]);
    runEffect(Smooth, d, host());
    // index 1 = (0 + 40)/2 = 20. index 2 = (0 + 0)/2 = 0 using the ORIGINAL
    // index 1 (which was 0), not the just-written 20.
    // index 3 = (40 + 0)/2 = 20 using the ORIGINAL index 2.
    expect(read(d)).toEqual([0, 20, 0, 20, 0]);
  });

  // The subtle one: the original averages via a logical shift on offset-binary
  // (8-bit) / XOR-flipped excess (wide), both of which FLOOR.
  it("floors the average rather than truncating toward zero", () => {
    const d = doc8([[0, 0, -1, 0, 0]]);
    runEffect(Smooth, d, host());
    // index 1 = (0 + -1)/2. C's signed divide gives 0; a logical shift gives -1.
    expect(read(d)[1]).toBe(-1);
  });

  it("floors at 16-bit too", () => {
    const d = new SoundDocument({ bps: 16, numChans: 1 });
    d.channels[0]!.data = new Uint8Array(10);
    setSample(d.channels[0]!.data, 0, 0, 16);
    setSample(d.channels[0]!.data, 2, 0, 16);
    setSample(d.channels[0]!.data, 4, -1, 16);
    setSample(d.channels[0]!.data, 6, 0, 16);
    setSample(d.channels[0]!.data, 8, 0, 16);
    d.firstSelCh = 1;
    d.lastSelCh = 1;
    runEffect(Smooth, d, host());
    expect(getSample(d.channels[0]!.data, 2, 16)).toBe(-1);
  });

  it("does nothing to a selection under three frames", () => {
    const d = doc8([[10, 90]]);
    runEffect(Smooth, d, host());
    expect(read(d)).toEqual([10, 90]);
  });

  it("never changes the channel's length", () => {
    const d = doc8([[1, 2, 3, 4]]);
    runEffect(Smooth, d, host());
    expect(d.channels[0]!.data.length).toBe(4);
  });
});

describe("Robotize", () => {
  /** A doc at exactly the native Fixed rate, where the division cancels. */
  const nativeDoc = (samples: number[]) => {
    const d = new SoundDocument({ bps: 8, numChans: 1, rate: NATIVE_RATE });
    d.channels[0]!.data = Uint8Array.from(samples.map((s) => (s ^ 0x80) & 0xff));
    d.firstSelCh = 1;
    d.lastSelCh = 1;
    return d;
  };

  it("uses a 190-sample delay at the Mac's native rate", () => {
    const samples = new Array(400).fill(0);
    samples[0] = 100;
    const d = nativeDoc(samples);
    runEffect(Robotize, d, host());
    // y[190] += trunc(100*75/100) = 75
    expect(read(d)[190]).toBe(75);
  });

  // The truncation has a sharp edge worth pinning: a rate a hair under native
  // drops the delay to 189. Not a rounding bug on our side — trunc() is what the
  // binary does, and 190*0.99999... is 189.99...
  it("truncates the delay, so a rate one tick below native gives 189", () => {
    const samples = new Array(400).fill(0);
    samples[0] = 100;
    const d = new SoundDocument({ bps: 8, numChans: 1, rate: NATIVE_RATE - 1 });
    d.channels[0]!.data = Uint8Array.from(samples.map((s) => (s ^ 0x80) & 0xff));
    d.firstSelCh = 1;
    d.lastSelCh = 1;
    runEffect(Robotize, d, host());
    expect(read(d)[189]).toBe(75);
    expect(read(d)[190]).toBe(0);
  });

  it("rescales the delay with the sample rate, holding ~8.54 ms", () => {
    const samples = new Array(800).fill(0);
    samples[0] = 100;
    const d = doc8([samples], 44100);
    runEffect(Robotize, d, host());
    // D = trunc(190 * 44100 / 22254.5454) = 376
    expect(read(d)[376]).toBe(75);
  });

  it("is recursive — one pass builds the whole comb train", () => {
    const samples = new Array(600).fill(0);
    samples[0] = 100;
    const d = nativeDoc(samples);
    runEffect(Robotize, d, host());
    // The write at 190 is read again as a source when the cursor reaches it.
    expect(read(d)[190]).toBe(75);
    expect(read(d)[380]).toBe(56); // trunc(75*75/100)
  });

  it("is a silent no-op on selections shorter than the delay", () => {
    const d = doc8([[10, 20, 30]]);
    runEffect(Robotize, d, host());
    expect(read(d)).toEqual([10, 20, 30]);
  });
});

describe("Noise", () => {
  it("replaces the selection rather than adding to it", () => {
    const d = doc8([new Array(64).fill(100)]);
    runEffect(Noise, d, host());
    // If it added, everything would have clamped to 127.
    const vals = read(d);
    expect(vals.every((v) => v === 127)).toBe(false);
  });

  it("only touches the selection", () => {
    const d = doc8([new Array(32).fill(50)]);
    setSelection(d, 8, 24);
    runEffect(Noise, d, host());
    const vals = read(d);
    expect(vals.slice(0, 8).every((v) => v === 50)).toBe(true);
    expect(vals.slice(24).every((v) => v === 50)).toBe(true);
  });

  it("fills an odd-length range completely, at any depth", () => {
    const d = doc8([new Array(16).fill(0)]);
    setSelection(d, 1, 8); // odd start, odd length
    runEffect(Noise, d, host());
    expect(d.channels[0]!.data.length).toBe(16);
  });
});

describe("Dither", () => {
  // The formula validated against the About text's own examples.
  it("reproduces the About text: 10% is 12 points at 8-bit, 3276 at 16-bit", () => {
    const s = { mode: DitherMode.percent, percent: 10, points: 0 };
    expect(ditherAmplitude(s, 8)).toBe(12);
    expect(ditherAmplitude(s, 16)).toBe(3276);
  });

  it("uses points directly in points mode", () => {
    expect(ditherAmplitude({ mode: DitherMode.points, percent: 0, points: 99 }, 8)).toBe(99);
  });

  it("adds noise rather than replacing the signal", () => {
    const d = doc8([new Array(64).fill(50)]);
    runEffect(Dither, d, host(), { mode: DitherMode.percent, percent: 10 });
    const vals = read(d);
    // Still centred near 50 — a replace would have scattered across the range.
    expect(vals.every((v) => Math.abs(v - 50) <= 13)).toBe(true);
    expect(vals.some((v) => v !== 50)).toBe(true);
  });
});

describe("the IIR filters", () => {
  it("quantises coefficients to one decimal and truncates — type 0.55, get 0.5", () => {
    const a = doc8([[0, 100, 100, 100]]);
    const b = doc8([[0, 100, 100, 100]]);
    runEffect(LowPassIIR, a, host(), { a: 0.55, b: 0 });
    runEffect(LowPassIIR, b, host(), { a: 0.5, b: 0 });
    expect(read(a)).toEqual(read(b));
  });

  it("seeds y(n-1) with the first sample and leaves it unmodified", () => {
    const d = doc8([[42, 0, 0]]);
    runEffect(LowPassIIR, d, host(), { a: 0, b: 1.0 });
    // First sample untouched; y(1) = 0 + (10*42)/10 = 42 from the seed, not 0.
    expect(read(d)[0]).toBe(42);
    expect(read(d)[1]).toBe(42);
  });

  it("subtracts for high pass, adds for low pass", () => {
    const hp = doc8([[10, 100]]);
    const lp = doc8([[10, 100]]);
    runEffect(HighPassIIR, hp, host(), { a: 1.0, b: 1.0 });
    runEffect(LowPassIIR, lp, host(), { a: 1.0, b: 1.0 });
    // hp: (10*100)/10 - (10*10)/10 = 100 - 10 = 90
    // lp: (10*100)/10 + (10*10)/10 = 100 + 10 = 110 -> clamped to 127
    expect(read(hp)[1]).toBe(90);
    expect(read(lp)[1]).toBe(110);
  });

  it("gives Shelving a feed-forward tap on the ORIGINAL input", () => {
    const d = doc8([[10, 50, 60]]);
    runEffect(Shelving, d, host(), { a: 1.0, b: 0 });
    // y(1) = x(1) + (10*x(0))/10 = 50 + 10 = 60
    // y(2) = x(2) + (10*x(1))/10 where x(1) is the ORIGINAL 50, not the written 60
    expect(read(d)[1]).toBe(60);
    expect(read(d)[2]).toBe(110);
  });

  it("does not validate coefficients — an unstable filter is your privilege", () => {
    const d = doc8([[10, 10, 10, 10, 10, 10]]);
    const r = runEffect(LowPassIIR, d, host(), { a: 1.0, b: 2.0 });
    expect(r).toBe(kModNoError); // no complaint
    expect(read(d)[5]).toBe(127); // blown up and clamped into a square wave
  });
});

describe("Echo and Delay — the same kernel, opposite directions", () => {
  const extendingHost = () => {
    const h = new Host();
    h.doExtend = DoExtend.always;
    return h;
  };
  const nonExtendingHost = () => {
    const h = new Host();
    h.doExtend = DoExtend.never;
    return h;
  };

  it("Delay mixes exactly ONE copy — no repeats", () => {
    const d = doc8([[100, 0, 0, 0, 0, 0]]);
    runEffect(Delay, d, nonExtendingHost(), { delay: 2, strength: 50, units: Unit.bytes });
    // One tap at +2: 100*50/100 = 50. Nothing at +4.
    expect(read(d)).toEqual([100, 0, 50, 0, 0, 0]);
  });

  it("Echo builds a whole decaying train from one pass", () => {
    const d = doc8([[100, 0, 0, 0, 0, 0, 0, 0]]);
    runEffect(Echo, d, nonExtendingHost(), {
      delay: 2,
      strength: 50,
      onlyOn: false,
      onlyCount: 0,
      units: Unit.bytes,
    });
    // 100 -> 50 at +2 -> 25 at +4 -> 12 at +6. The recursion IS the decay.
    expect(read(d)).toEqual([100, 0, 50, 0, 25, 0, 12, 0]);
  });

  it("Echo extends the buffer by the decay range when allowed", () => {
    const d = doc8([[100, 0]]);
    const before = d.channels[0]!.data.length;
    runEffect(Echo, d, extendingHost(), {
      delay: 2,
      strength: 50,
      onlyOn: false,
      onlyCount: 0,
      units: Unit.bytes,
    });
    expect(d.channels[0]!.data.length).toBeGreaterThan(before);
  });

  it("Echo confines the decay to the selection when extension is off", () => {
    const d = doc8([[100, 0, 0, 0]]);
    runEffect(Echo, d, nonExtendingHost(), {
      delay: 2,
      strength: 50,
      onlyOn: false,
      onlyCount: 0,
      units: Unit.bytes,
    });
    expect(d.channels[0]!.data.length).toBe(4);
  });

  it("'Only N echoes' caps the range with no decay maths", () => {
    const d = doc8([[100, 0]]);
    runEffect(Echo, d, extendingHost(), {
      delay: 2,
      strength: 50,
      onlyOn: true,
      onlyCount: 1,
      units: Unit.bytes,
    });
    expect(d.channels[0]!.data.length).toBe(4); // end + 1*delay
  });

  it("silences a grown tail before mixing into it", () => {
    const d = doc8([[100, 0]]);
    runEffect(Delay, d, extendingHost(), { delay: 2, strength: 50, units: Unit.bytes });
    // The new tail must be silence + the echo, not uninitialised garbage.
    expect(read(d)).toEqual([100, 0, 50, 0]);
  });

  it("frame-aligns the delay", () => {
    const d = new SoundDocument({ bps: 16, numChans: 1 });
    d.channels[0]!.data = new Uint8Array(12);
    setSample(d.channels[0]!.data, 0, 100, 16);
    d.firstSelCh = 1;
    d.lastSelCh = 1;
    runEffect(Delay, d, nonExtendingHost(), { delay: 5, strength: 50, units: Unit.bytes });
    // 5 bytes frame-aligns down to 4 = 2 frames.
    expect(getSample(d.channels[0]!.data, 4, 16)).toBe(50);
  });
});

describe("Weird Tempo", () => {
  it("finds segment boundaries at falling zero-crossings, zero counting as positive", () => {
    // +,+,-,-,+,+,-,-
    const d = doc8([[10, 10, -10, -10, 10, 10, -10, -10]]);
    const bounds = segmentBoundaries(d.channels[0]!.data, 0, 8, 8);
    expect(bounds).toEqual([2, 6]);
  });

  it("doubles each complete segment", () => {
    const d = doc8([[10, 10, -10, -10, 10, 10, -10, -10]]);
    runEffect(WeirdTempo, d, host(), {});
    // segments [0,2) and [2,6) doubled, trailing [6,8) once.
    expect(read(d)).toEqual([
      10, 10, 10, 10, // [0,2) twice
      -10, -10, 10, 10, -10, -10, 10, 10, // [2,6) twice
      -10, -10, // trailing, once
    ]);
  });

  it("leaves unipolar material completely alone — no falling crossing ever fires", () => {
    const d = doc8([[10, 20, 30, 40]]);
    runEffect(WeirdTempo, d, host(), {});
    expect(read(d)).toEqual([10, 20, 30, 40]);
  });

  it("leaves digital silence alone", () => {
    const d = doc8([[0, 0, 0, 0]]);
    runEffect(WeirdTempo, d, host(), {});
    expect(read(d)).toEqual([0, 0, 0, 0]);
  });

  it("warns on EVERY run, not just the first", () => {
    const h = host();
    const d = doc8([[10, -10, 10, -10]]);
    runEffect(WeirdTempo, d, h, {});
    runEffect(WeirdTempo, d, h, {});
    expect(h.errors.filter((e) => e.errNum === 3)).toHaveLength(2);
    expect(h.errors[0]!.withCancel).toBe(true);
  });

  it("does nothing if the warning is cancelled", () => {
    const d = doc8([[10, -10, 10, -10]]);
    const r = runEffect(WeirdTempo, d, host(), { confirm: () => false });
    expect(r).toBe(kModCancel);
    expect(read(d)).toEqual([10, -10, 10, -10]);
  });

  it("ignores doExtend and lengthens anyway", () => {
    const h = host();
    h.doExtend = DoExtend.never;
    const d = doc8([[10, -10, 10, -10]]);
    runEffect(WeirdTempo, d, h, {});
    expect(d.channels[0]!.data.length).toBeGreaterThan(4);
  });
});

describe("the simple modules", () => {
  it("Invert negates, and the two's-complement asymmetry survives", () => {
    const d = doc8([[10, -10, -128]]);
    runEffect(Invert, d, host());
    // -(-128) = 128 clamps to 127: the asymmetry is preserved, not special-cased.
    expect(read(d)).toEqual([-10, 10, 127]);
  });

  it("Reverse reverses the selection only", () => {
    const d = doc8([[1, 2, 3, 4, 5, 6]]);
    setSelection(d, 1, 5);
    runEffect(Reverse, d, host());
    expect(read(d)).toEqual([1, 5, 4, 3, 2, 6]);
  });

  it("Reverse handles 16-bit frames as units", () => {
    const d = new SoundDocument({ bps: 16, numChans: 1 });
    d.channels[0]!.data = new Uint8Array(6);
    setSample(d.channels[0]!.data, 0, 1, 16);
    setSample(d.channels[0]!.data, 2, 2, 16);
    setSample(d.channels[0]!.data, 4, 3, 16);
    d.firstSelCh = 1;
    d.lastSelCh = 1;
    runEffect(Reverse, d, host());
    expect(getSample(d.channels[0]!.data, 0, 16)).toBe(3);
    expect(getSample(d.channels[0]!.data, 4, 16)).toBe(1);
  });

  it("Silence writes the depth's silent byte", () => {
    const d = doc8([[10, 20]]);
    runEffect(Silence, d, host());
    expect(Array.from(d.channels[0]!.data)).toEqual([0x80, 0x80]);
  });

  it("Draw Line ramps from the first sample to the last", () => {
    const d = doc8([[0, 99, 99, 40]]);
    runEffect(DrawLine, d, host());
    expect(read(d)).toEqual([0, 13, 26, 40]);
  });

  it("Normalize scales up to full scale without clipping", () => {
    const d = doc8([[0, 60, -30]]);
    runEffect(Normalize, d, host(), { scope: 1 });
    // gain = trunc(127*100/60) = 211; 60*211/100 = 126.6 -> 126.
    // The negative truncates TOWARD ZERO (-63.3 -> -63), as C's signed divide
    // does and Gain.c relies on — not floor, which would give -64. Smooth is the
    // odd one out there, and only because it halves via a logical shift.
    expect(read(d)).toEqual([0, 126, -63]);
  });

  it("Normalize declines on digital silence rather than dividing by zero", () => {
    const d = doc8([[0, 0]]);
    expect(runEffect(Normalize, d, host(), { scope: 1 })).toBe(kModCancel);
  });

  it("Normalize declines when already at full scale", () => {
    const d = doc8([[127, 0]]);
    expect(runEffect(Normalize, d, host(), { scope: 1 })).toBe(kModCancel);
  });

  it("Low Pass FIR averages with the previous sample", () => {
    const d = doc8([[0, 100, 0]]);
    runEffect(LowPassFIR, d, host());
    // y(1) = (100+0)/2 = 50; y(2) = (0+100)/2 = 50 using the original x(1).
    expect(read(d)).toEqual([0, 50, 50]);
  });
});

describe("the channel modules", () => {
  it("Sum adds the first selected channel into the second", () => {
    const d = doc8([
      [10, 20],
      [1, 2],
    ]);
    runEffect(Sum, d, host());
    expect(read(d, 0)).toEqual([10, 20]); // ch1 untouched
    expect(read(d, 1)).toEqual([11, 22]);
  });

  it("Sum zero-pads where a channel has run out", () => {
    const d = doc8([
      [10],
      [1, 2],
    ]);
    runEffect(Sum, d, host());
    // ch1 contributes digital zero past its end, rather than being skipped.
    expect(read(d, 1)).toEqual([11, 2]);
  });

  it("Sum needs two selected channels", () => {
    const d = doc8([[10, 20]]);
    expect(runEffect(Sum, d, host())).toBe(kModCancel);
  });

  it("Selection to Channel appends a copy per selected channel", () => {
    const d = doc8([[1, 2, 3, 4]]);
    setSelection(d, 1, 3);
    runEffect(SelectionToChannel, d, host());
    expect(d.numChans).toBe(2);
    expect(read(d, 1)).toEqual([2, 3]);
  });
});

describe("the Effects Folder", () => {
  it("groups modules into the recovered submenus", () => {
    const menu = buildEffectMenu();
    const groups = menu.groups.map((g) => g.name);
    expect(groups).toContain("Echoes & Reverbs");
    expect(groups).toContain("FIR/IIR Filters");
    expect(groups).toContain("Resampling");
    expect(groups).toContain("Misc");
  });

  it("puts Delay, Echo and Flanger in Echoes & Reverbs", () => {
    const menu = buildEffectMenu();
    const g = menu.groups.find((x) => x.name === "Echoes & Reverbs")!;
    expect(g.modules.map((m) => m.name)).toEqual(["Delay", "Echo", "Flanger"]);
  });

  it("leaves ungrouped modules at top level", () => {
    const menu = buildEffectMenu();
    expect(menu.topLevel.map((m) => m.name)).toContain("Amplify");
    expect(menu.topLevel.map((m) => m.name)).toContain("Weird Tempo");
  });

  it("templates the menu items", () => {
    expect(optionsItemTitle(Amplify as never)).toBe("Amplify Options…");
    expect(aboutItemTitle(Amplify as never)).toBe("About Amplify…");
  });

  it("finds a module by the signature its prefs are keyed by", () => {
    expect(findBySignature("SampleEffect")).toBe(Amplify);
  });

  it("gives every module a unique signature", () => {
    const sigs = EFFECTS.map((m) => m.signature);
    expect(new Set(sigs).size).toBe(sigs.length);
  });
});
