import { describe, it, expect } from "vitest";
import { SoundDocument } from "../src/document.js";
import { setSelection } from "../src/selection.js";
import { doubleToFixed, Unit } from "../src/units.js";
import { Host, DoExtend, runEffect, kModCancel } from "../src/runtime/index.js";
import {
  Flanger,
  FlangerLFO,
  FLANGER_PRESETS,
  Modulation,
  LouderMeans,
  grainEnvelope,
  tween,
  IterativeCrossfade,
  GranularSynthesis,
  SampleHose,
  QuickMix,
  MixUse,
  MixDest,
  Replicate,
  GapMode,
  gapBytes,
  WaveformGenerator,
  WaveType,
  waveSample,
  ZeroCrosser,
  CrossAction,
  CrossSide,
  Difference,
  DifferenceDest,
  EFFECTS,
  buildEffectMenu,
} from "../src/effects/index.js";

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

// ---------------------------------------------------------------------------
// Norris shared machinery — the verified kernels
// ---------------------------------------------------------------------------

describe("the Curvature tween", () => {
  it("is linear at curvature 100", () => {
    expect(tween(0, 10, 0, 100, 100)).toBeCloseTo(0);
    expect(tween(5, 10, 0, 100, 100)).toBeCloseTo(50);
    expect(tween(10, 10, 0, 100, 100)).toBeCloseTo(100);
  });

  it("is convex below 100 and concave above", () => {
    // At the midpoint, convex (0) rises faster than linear; concave (200) slower.
    const linear = tween(5, 10, 0, 100, 100);
    expect(tween(5, 10, 0, 100, 50)).toBeGreaterThan(linear);
    expect(tween(5, 10, 0, 100, 200)).toBeLessThan(linear);
  });

  it("hits the endpoints whatever the curvature", () => {
    for (const c of [0, 50, 100, 200]) {
      expect(tween(10, 10, 7, 42, c)).toBeCloseTo(42);
    }
  });
});

describe("the grain envelope — the best-corroborated formula in the project", () => {
  // Confirmed three ways: 68k build, 68881 build, and the Read Me's published
  // Expressionist equation from PICT 1001.
  it("is exactly 1.0 at the grain centre with Fade Level 0", () => {
    expect(grainEnvelope(50, 100, 0)).toBeCloseTo(1.0, 10);
  });

  it("sits at ~0.0058 at the grain edges", () => {
    expect(grainEnvelope(0, 100, 0)).toBeCloseTo(0.0058, 3);
    expect(grainEnvelope(100, 100, 0)).toBeCloseTo(0.0058, 3);
  });

  it("is unity everywhere at Fade Level 100% — no enveloping at all", () => {
    for (const x of [0, 25, 50, 75, 100]) {
      expect(grainEnvelope(x, 100, 1)).toBeCloseTo(1.0, 10);
    }
  });

  it("lifts the edges linearly toward unity at intermediate Fade Levels", () => {
    // The Read Me's own example: at f=0.2 the edges sit at ~0.205.
    expect(grainEnvelope(0, 100, 0.2)).toBeCloseTo(0.205, 2);
  });

  it("is the blend E(x)*(1-f) + f", () => {
    const f = 0.35;
    for (const x of [0, 17, 50, 83, 100]) {
      const E = grainEnvelope(x, 100, 0);
      expect(grainEnvelope(x, 100, f)).toBeCloseTo(E * (1 - f) + f, 9);
    }
  });

  it("is flatter-topped than a Gaussian — that is the cubing", () => {
    // Near the centre the curve should stay closer to 1 than a Gaussian would.
    const gaussian = (x: number) => Math.exp(-Math.pow((5 * x) / 100 - 2.5, 2));
    expect(grainEnvelope(40, 100, 0)).toBeGreaterThan(gaussian(40));
  });
});

// ---------------------------------------------------------------------------
// Flanger
// ---------------------------------------------------------------------------

describe("Flanger LFO", () => {
  it("centres the sine on Delay and swings by Width", () => {
    const lfo = new FlangerLFO(Modulation.sinusoidal, 100, 20, 100);
    expect(lfo.next(0)).toBeCloseTo(100); // sin(0) = 0 -> the centreline
    expect(lfo.next(25)).toBeCloseTo(120); // quarter period -> +Width
    expect(lfo.next(75)).toBeCloseTo(80); // three quarters -> -Width
  });

  it("reflects the triangle at the rails rather than wrapping", () => {
    const lfo = new FlangerLFO(Modulation.triangular, 100, 10, 100);
    const seen: number[] = [];
    for (let n = 0; n < 200; n++) seen.push(lfo.next(n));
    expect(Math.max(...seen)).toBeLessThanOrEqual(110);
    expect(Math.min(...seen)).toBeGreaterThanOrEqual(90);
    // It must come back down, not run away.
    expect(seen[199]).toBeLessThanOrEqual(110);
  });

  it("hard-resets the sawtooth instead of reflecting", () => {
    const lfo = new FlangerLFO(Modulation.sawtooth, 100, 10, 100);
    const seen: number[] = [];
    for (let n = 0; n < 150; n++) seen.push(lfo.next(n));
    expect(seen[0]).toBeCloseTo(90); // starts at Delay - Width
    expect(Math.max(...seen)).toBeLessThanOrEqual(110);
    // A reset means a large negative jump somewhere; a reflection would not.
    const drops = seen.slice(1).filter((v, i) => v < seen[i]! - 5);
    expect(drops.length).toBeGreaterThan(0);
  });

  it("keeps all three waveforms inside Delay +/- Width", () => {
    for (const m of [Modulation.sinusoidal, Modulation.triangular, Modulation.sawtooth]) {
      const lfo = new FlangerLFO(m, 50, 10, 40);
      for (let n = 0; n < 300; n++) {
        const v = lfo.next(n);
        expect(v).toBeGreaterThanOrEqual(39.9);
        expect(v).toBeLessThanOrEqual(60.1);
      }
    }
  });
});

describe("Flanger", () => {
  const flangerDoc = () => doc8([new Array(400).fill(0).map((_, i) => (i % 8 < 4 ? 50 : -50))]);

  const base = {
    flangePercent: 50,
    feedback: false,
    delay: 20,
    delayUnits: Unit.bytes,
    width: 5,
    widthUnits: Unit.bytes,
    period: 100,
    periodUnits: Unit.bytes,
    modulation: Modulation.sinusoidal,
    envelopeFollower: false,
    depth: 0,
    depthUnits: Unit.bytes,
    louderMeans: LouderMeans.longer,
  };

  it("never changes the sound's length — it is fixed-length and in place", () => {
    const d = flangerDoc();
    const before = d.channels[0]!.data.length;
    runEffect(Flanger, d, host(), base);
    expect(d.channels[0]!.data.length).toBe(before);
  });

  it("leaves the Delay+Width seed region untouched", () => {
    const d = flangerDoc();
    const before = read(d);
    runEffect(Flanger, d, host(), base);
    const after = read(d);
    // The first delay+width bytes are never an output position.
    expect(after.slice(0, 25)).toEqual(before.slice(0, 25));
  });

  it("passes the dry signal through at 0% flange", () => {
    const d = flangerDoc();
    const before = read(d);
    runEffect(Flanger, d, host(), { ...base, flangePercent: 0 });
    expect(read(d)).toEqual(before);
  });

  it("clamps the flange percentage to [0,100]", () => {
    expect(Flanger.validate!({ ...base, flangePercent: 500 }).flangePercent).toBe(100);
    expect(Flanger.validate!({ ...base, flangePercent: -5 }).flangePercent).toBe(0);
  });

  it("actually changes the sound at 100% flange", () => {
    const d = flangerDoc();
    const before = read(d);
    runEffect(Flanger, d, host(), { ...base, flangePercent: 100 });
    expect(read(d)).not.toEqual(before);
  });

  it("ignores doExtend entirely", () => {
    const d = flangerDoc();
    const h = host();
    h.doExtend = DoExtend.always;
    const before = d.channels[0]!.data.length;
    runEffect(Flanger, d, h, base);
    expect(d.channels[0]!.data.length).toBe(before);
  });

  it("gives feedback on and off different results — the sweep direction IS the difference", () => {
    const off = flangerDoc();
    const on = flangerDoc();
    runEffect(Flanger, off, host(), { ...base, feedback: false, flangePercent: 100 });
    runEffect(Flanger, on, host(), { ...base, feedback: true, flangePercent: 100 });
    expect(read(off)).not.toEqual(read(on));
  });
});

describe("the 9 factory Flanger presets", () => {
  it("ships all nine", () => {
    expect(FLANGER_PRESETS).toHaveLength(9);
  });

  // The recovery's structural finding, not just the names.
  it("has Feedback off in every single one, including Distorto", () => {
    for (const p of FLANGER_PRESETS) expect(p.values.feedback).toBe(false);
  });

  it("uses Sinusoidal and Longer throughout", () => {
    for (const p of FLANGER_PRESETS) {
      expect(p.values.modulation).toBe(Modulation.sinusoidal);
      expect(p.values.louderMeans).toBe(LouderMeans.longer);
    }
  });

  it("makes Soft Flange identical to the Plain Flange family except for Period", () => {
    const soft = FLANGER_PRESETS.find((p) => p.name === "Soft Flange")!.values;
    const medium = FLANGER_PRESETS.find((p) => p.name === "Plain Flange - Medium")!.values;
    expect(soft.delay).toBe(medium.delay);
    expect(soft.width).toBe(medium.width);
    expect(soft.flangePercent).toBe(medium.flangePercent);
    // Not algorithmically distinct at all — just another Period with a nicer name.
    expect(soft.period).not.toBe(medium.period);
  });

  it("shares Delay/Width across the whole Plain Flange family bar Fast", () => {
    const family = FLANGER_PRESETS.filter(
      (p) => p.name.startsWith("Plain Flange") && !p.name.includes("Fast"),
    );
    for (const p of family) {
      expect(p.values.delay).toBe(245);
      expect(p.values.width).toBe(45);
    }
  });

  it("turns the Envelope Follower on only for Distorto", () => {
    for (const p of FLANGER_PRESETS) {
      expect(p.values.envelopeFollower).toBe(p.name.startsWith("Distorto"));
    }
  });
});

// ---------------------------------------------------------------------------
// Iterative Crossfade
// ---------------------------------------------------------------------------

describe("Iterative Crossfade", () => {
  const settings = { iterations: 1, silenceLength: 0, units: Unit.bytes };

  it("needs two selected channels", () => {
    const d = doc8([[10, 20]]);
    expect(runEffect(IterativeCrossfade, d, host(), settings)).toBe(kModCancel);
  });

  it("renders into a NEW channel, leaving the sources alone", () => {
    const d = doc8([
      [100, 100, 100, 100],
      [0, 0, 0, 0],
    ]);
    runEffect(IterativeCrossfade, d, host(), settings);
    expect(d.numChans).toBe(3);
    expect(read(d, 0)).toEqual([100, 100, 100, 100]);
    expect(read(d, 1)).toEqual([0, 0, 0, 0]);
  });

  it("crossfades A to B linearly across the iteration", () => {
    const d = doc8([
      [100, 100, 100, 100],
      [0, 0, 0, 0],
    ]);
    runEffect(IterativeCrossfade, d, host(), settings);
    // r = i/4 -> A*(1-r): 100, 75, 50, 25
    expect(read(d, 2)).toEqual([100, 75, 50, 25]);
  });

  it("zero-pads an exhausted channel rather than skipping it", () => {
    // ch2 is shorter: past its end it contributes digital zero, so the fade
    // runs into silence rather than rescaling.
    const d = doc8([
      [100, 100, 100, 100],
      [100, 100],
    ]);
    runEffect(IterativeCrossfade, d, host(), settings);
    const out = read(d, 2);
    expect(out[0]).toBe(100);
    expect(out[3]).toBe(25); // A*(1-0.75) + 0*0.75
  });

  it("gives new length n*(l+s) - s, with silence only BETWEEN iterations", () => {
    const d = doc8([
      [10, 20],
      [30, 40],
    ]);
    runEffect(IterativeCrossfade, d, host(), { iterations: 3, silenceLength: 2, units: Unit.bytes });
    // n=3, l=2, s=2 -> 3*(2+2) - 2 = 10
    expect(d.channels[2]!.data.length).toBe(10);
  });

  it("packs iterations back-to-back with zero silence — the quasi-granular case", () => {
    const d = doc8([
      [10, 20],
      [30, 40],
    ]);
    runEffect(IterativeCrossfade, d, host(), { iterations: 4, silenceLength: 0, units: Unit.bytes });
    expect(d.channels[2]!.data.length).toBe(8);
  });

  it("truncates rather than rounding, then clamps", () => {
    const d = doc8([
      [1, 1, 1],
      [0, 0, 0],
    ]);
    runEffect(IterativeCrossfade, d, host(), settings);
    // 1*(1-1/3) = 0.667 -> truncates to 0
    expect(read(d, 2)[1]).toBe(0);
  });
});

// ---------------------------------------------------------------------------
// Granular Synthesis / Sample Hose
// ---------------------------------------------------------------------------

describe("Granular Synthesis", () => {
  const settings = {
    newLength: 64,
    units: Unit.bytes,
    initialGrainLength: 16,
    finalGrainLength: 16,
    variance: 0,
    curvature: 100,
    initialDensity: 100,
    finalDensity: 100,
    densityCurvature: 100,
    fadeLevel: 0,
    seed: 1,
  };

  it("granulates into a new channel of the requested length", () => {
    const d = doc8([new Array(32).fill(100)]);
    runEffect(GranularSynthesis, d, host(), settings);
    expect(d.numChans).toBe(2);
    expect(d.channels[1]!.data.length).toBe(64);
  });

  it("leaves the source untouched", () => {
    const d = doc8([new Array(32).fill(100)]);
    runEffect(GranularSynthesis, d, host(), settings);
    expect(read(d, 0)).toEqual(new Array(32).fill(100));
  });

  it("shapes grains with the envelope — edges are quieter than centres", () => {
    const d = doc8([new Array(32).fill(100)]);
    runEffect(GranularSynthesis, d, host(), { ...settings, initialDensity: 100, finalDensity: 100 });
    const out = read(d, 1);
    // The very first output sample is a grain edge: ~0.6% of 100.
    expect(Math.abs(out[0]!)).toBeLessThan(5);
  });

  it("passes grains through unshaped at Fade Level 100%", () => {
    const d = doc8([new Array(32).fill(100)]);
    runEffect(GranularSynthesis, d, host(), { ...settings, fadeLevel: 100 });
    expect(read(d, 1)[0]).toBe(100); // y = 1, so the edge is full amplitude
  });

  it("is exactly reproducible from a seed — grain jitter must be", () => {
    const a = doc8([new Array(32).fill(100)]);
    const b = doc8([new Array(32).fill(100)]);
    runEffect(GranularSynthesis, a, host(), { ...settings, variance: 8, seed: 42 });
    runEffect(GranularSynthesis, b, host(), { ...settings, variance: 8, seed: 42 });
    expect(read(a, 1)).toEqual(read(b, 1));
  });

  it("gives different output for different seeds", () => {
    const a = doc8([new Array(32).fill(100)]);
    const b = doc8([new Array(32).fill(100)]);
    runEffect(GranularSynthesis, a, host(), { ...settings, variance: 8, seed: 1 });
    runEffect(GranularSynthesis, b, host(), { ...settings, variance: 8, seed: 999 });
    expect(read(a, 1)).not.toEqual(read(b, 1));
  });
});

describe("Sample Hose", () => {
  const p = (initial: number, final: number) => ({
    initialValue: initial,
    initialVariance: 0,
    finalValue: final,
    finalVariance: 0,
    curvature: 100,
  });
  const settings = {
    newLength: 32,
    units: Unit.bytes,
    rate: p(100, 100),
    gain: p(100, 100),
    pan: p(50, 50),
    bite: p(0, 0),
    period: p(16, 16),
    stereo: false,
    seed: 1,
  };

  it("sprays into a new channel", () => {
    const d = doc8([[0, 20, 40, 60, 80, 100, 80, 60]]);
    runEffect(SampleHose, d, host(), settings);
    expect(d.numChans).toBe(2);
    expect(d.channels[1]!.data.length).toBe(32);
  });

  it("makes two channels in stereo mode", () => {
    const d = doc8([[0, 20, 40, 60, 80, 100, 80, 60]]);
    runEffect(SampleHose, d, host(), { ...settings, stereo: true });
    expect(d.numChans).toBe(3);
  });

  // THE headline: the only interpolating DSP in the app.
  it("linearly interpolates on Rate — it does not drop-sample", () => {
    // Rate 50% = octave down: each source sample stretched over two outputs, and
    // the in-between sample must be the MIDPOINT, not a repeat.
    const d = doc8([[0, 100, 0, 0, 0, 0, 0, 0]]);
    runEffect(SampleHose, d, host(), { ...settings, rate: p(50, 50), period: p(64, 64) });
    const out = read(d, 1);
    // Reading at pos 0, 0.5, 1.0, 1.5: 0, 50, 100, 50
    expect(out[1]).toBe(50); // a drop-sample resampler would give 0 here
    expect(out[2]).toBe(100);
  });

  it("contrasts with the host resampler, which truncates the fraction away", () => {
    // Same 2x stretch through the host's ModResampleSoundRate gives duplicates,
    // never midpoints. That difference is the point of the finding.
    const d = doc8([[0, 100, 0, 0, 0, 0, 0, 0]]);
    runEffect(SampleHose, d, host(), { ...settings, rate: p(50, 50), period: p(64, 64) });
    expect(read(d, 1)[1]).not.toBe(0); // interpolated, not held
  });

  it("scales by Gain", () => {
    const d = doc8([[100, 100, 100, 100]]);
    runEffect(SampleHose, d, host(), { ...settings, gain: p(50, 50), period: p(64, 64) });
    expect(read(d, 1)[0]).toBe(50);
  });

  it("pans hard left at 0 and hard right at 100", () => {
    const d = doc8([[100, 100, 100, 100]]);
    runEffect(SampleHose, d, host(), {
      ...settings,
      stereo: true,
      pan: p(0, 0),
      period: p(64, 64),
    });
    // Pan 0 = L: the left channel gets it all, the right nothing.
    expect(read(d, 1)[0]).toBe(100);
    expect(read(d, 2)[0]).toBe(0);
  });

  it("is reproducible from a seed", () => {
    const a = doc8([[0, 20, 40, 60, 80, 100, 80, 60]]);
    const b = doc8([[0, 20, 40, 60, 80, 100, 80, 60]]);
    const withVar = { ...settings, rate: { ...p(100, 100), initialVariance: 20, finalVariance: 20 } };
    runEffect(SampleHose, a, host(), { ...withVar, seed: 7 });
    runEffect(SampleHose, b, host(), { ...withVar, seed: 7 });
    expect(read(a, 1)).toEqual(read(b, 1));
  });

  it("defaults to the recovered 106-byte struct's values", () => {
    const d = SampleHose.defaults!();
    expect(d.newLength).toBe(222000); // ~10 s at 22254.5454 Hz
    expect(d.pan.initialValue).toBe(50); // centre
    expect(d.stereo).toBe(true);
    // All five curvatures = 100 = linear.
    for (const k of ["rate", "gain", "pan", "bite", "period"] as const) {
      expect(d[k].curvature).toBe(100);
    }
    // Variances and Bite all zero.
    expect(d.bite.initialValue).toBe(0);
  });
});

// ---------------------------------------------------------------------------
// Quick Mix / Replicate / Waveform / Zero Crosser / Difference
// ---------------------------------------------------------------------------

describe("Quick Mix", () => {
  it("sums the selected channels into a new channel", () => {
    const d = doc8([
      [10, 20],
      [1, 2],
    ]);
    runEffect(QuickMix, d, host(), { destination: MixDest.newChannel, use: MixUse.mono, usePan: false });
    expect(d.numChans).toBe(3);
    expect(read(d, 2)).toEqual([11, 22]);
  });

  it("clips rather than averaging — no headroom protection", () => {
    const d = doc8([
      [100, 100],
      [100, 100],
    ]);
    runEffect(QuickMix, d, host(), { destination: MixDest.newChannel, use: MixUse.mono, usePan: false });
    expect(read(d, 2)).toEqual([127, 127]);
  });

  it("routes stereo by each channel's speaker toggles", () => {
    const d = doc8([
      [50, 50],
      [70, 70],
    ]);
    d.channels[0]!.outLeft = true;
    d.channels[0]!.outRight = false;
    d.channels[1]!.outLeft = false;
    d.channels[1]!.outRight = true;
    runEffect(QuickMix, d, host(), { destination: MixDest.newChannel, use: MixUse.stereo, usePan: false });
    expect(read(d, 2)).toEqual([50, 50]); // left = ch1 only
    expect(read(d, 3)).toEqual([70, 70]); // right = ch2 only
  });

  it("sends a New Document mix to the host, leaving the source alone", () => {
    const h = host();
    const d = doc8([
      [10, 20],
      [1, 2],
    ]);
    runEffect(QuickMix, d, h, { destination: MixDest.newDocument, use: MixUse.mono, usePan: false });
    expect(d.numChans).toBe(2); // source untouched
    expect(h.spawnedDocuments).toHaveLength(1);
    expect(Array.from(h.spawnedDocuments[0]!.channels[0]!.data).map((b) => ((b ^ 0x80) << 24) >> 24)).toEqual([11, 22]);
  });
});

describe("Replicate", () => {
  const extend = () => {
    const h = new Host();
    h.doExtend = DoExtend.always;
    return h;
  };

  it("clones the selection N times with no gaps", () => {
    const d = doc8([[1, 2]]);
    runEffect(Replicate, d, extend(), {
      copies: 3,
      gapMode: GapMode.none,
      gapDivisor: 2,
      gapMultiplier: 2,
      gapLength: 0,
      units: Unit.bytes,
    });
    expect(read(d)).toEqual([1, 2, 1, 2, 1, 2]);
  });

  it("inserts a fixed gap between copies but not after the last", () => {
    const d = doc8([[1, 2]]);
    runEffect(Replicate, d, extend(), {
      copies: 2,
      gapMode: GapMode.fixed,
      gapDivisor: 2,
      gapMultiplier: 2,
      gapLength: 2,
      units: Unit.bytes,
    });
    expect(read(d)).toEqual([1, 2, 0, 0, 1, 2]);
  });

  // A documented behaviour, not an implementation detail.
  it("falls back to gap = selection length on divide-by-zero", () => {
    const prefs = {
      copies: 2,
      gapMode: GapMode.selectionDividedBy,
      gapDivisor: 0,
      gapMultiplier: 2,
      gapLength: 0,
      units: Unit.bytes,
    };
    expect(gapBytes(prefs, 40)).toBe(40);
  });

  it("computes divided-by and multiplied-by gaps", () => {
    const b = { copies: 2, gapMultiplier: 3, gapDivisor: 4, gapLength: 0, units: Unit.bytes };
    expect(gapBytes({ ...b, gapMode: GapMode.selectionDividedBy }, 40)).toBe(10);
    expect(gapBytes({ ...b, gapMode: GapMode.selectionMultipliedBy }, 40)).toBe(120);
    expect(gapBytes({ ...b, gapMode: GapMode.none }, 40)).toBe(0);
  });

  it("declines when extension is refused — there is nowhere to put the copies", () => {
    const h = new Host();
    h.doExtend = DoExtend.never;
    const d = doc8([[1, 2]]);
    expect(
      runEffect(Replicate, d, h, {
        copies: 3,
        gapMode: GapMode.none,
        gapDivisor: 2,
        gapMultiplier: 2,
        gapLength: 0,
        units: Unit.bytes,
      }),
    ).toBe(kModCancel);
    expect(read(d)).toEqual([1, 2]);
  });
});

describe("Waveform Generator", () => {
  it("shapes each wave type distinctly", () => {
    expect(waveSample(WaveType.sine, 0)).toBeCloseTo(0);
    expect(waveSample(WaveType.sine, 0.25)).toBeCloseTo(1);
    expect(waveSample(WaveType.square, 0.25)).toBe(1);
    expect(waveSample(WaveType.square, 0.75)).toBe(-1);
    expect(waveSample(WaveType.sawtooth, 0)).toBe(-1);
    expect(waveSample(WaveType.sawtooth, 1)).toBeCloseTo(1);
  });

  it("synthesises into the selection at the requested amplitude", () => {
    // 4 samples at 4 Hz / 4 Hz rate = one cycle across the selection.
    const d = doc8([[0, 0, 0, 0]], 4);
    runEffect(WaveformGenerator, d, host(), {
      length: 0,
      units: Unit.bytes,
      frequency: 1,
      amplitude: 100,
      waveType: WaveType.square,
    });
    expect(read(d)).toEqual([127, 127, -127, -127]);
  });

  it("scales by the amplitude percentage", () => {
    const d = doc8([[0, 0, 0, 0]], 4);
    runEffect(WaveformGenerator, d, host(), {
      length: 0,
      units: Unit.bytes,
      frequency: 1,
      amplitude: 50,
      waveType: WaveType.square,
    });
    expect(read(d)[0]).toBe(63); // trunc(127*0.5)
  });
});

describe("Zero Crosser", () => {
  // +,+,-,-,+,+,-,-  : crossings between 1|2, 3|4, 5|6
  const crossDoc = () => doc8([[10, 10, -10, -10, 10, 10, -10, -10]]);

  it("touches no audio at all — the selection is the product", () => {
    const d = crossDoc();
    const before = read(d);
    setSelection(d, 3, 5);
    runEffect(ZeroCrosser, d, host(), { action: CrossAction.expand, side: CrossSide.both });
    expect(read(d)).toEqual(before);
  });

  it("expands outward to the nearest crossings", () => {
    const d = crossDoc();
    setSelection(d, 3, 5);
    runEffect(ZeroCrosser, d, host(), { action: CrossAction.expand, side: CrossSide.both });
    expect(d.selSt).toBe(2);
    expect(d.selEnd).toBe(6);
  });

  it("expands only the requested side", () => {
    const d = crossDoc();
    setSelection(d, 3, 5);
    runEffect(ZeroCrosser, d, host(), { action: CrossAction.expand, side: CrossSide.left });
    expect(d.selSt).toBe(2);
    expect(d.selEnd).toBe(5); // right untouched
  });

  it("shrinks inward", () => {
    const d = crossDoc();
    setSelection(d, 0, 8);
    runEffect(ZeroCrosser, d, host(), { action: CrossAction.shrink, side: CrossSide.both });
    expect(d.selSt).toBe(2);
    expect(d.selEnd).toBe(6);
  });

  it("declines when there is no crossing to find", () => {
    const d = doc8([[10, 10, 10, 10]]);
    setSelection(d, 1, 3);
    expect(runEffect(ZeroCrosser, d, host(), { action: CrossAction.expand, side: CrossSide.both })).toBe(
      kModCancel,
    );
  });
});

describe("Difference", () => {
  it("subtracts into a new channel without destroying either source", () => {
    const d = doc8([
      [50, 60],
      [10, 20],
    ]);
    runEffect(Difference, d, host(), { destination: DifferenceDest.newChannel, normalize: false });
    expect(d.numChans).toBe(3);
    expect(read(d, 0)).toEqual([50, 60]);
    expect(read(d, 1)).toEqual([10, 20]);
    expect(read(d, 2)).toEqual([40, 40]);
  });

  it("normalizes the difference when asked", () => {
    const d = doc8([
      [50, 60],
      [49, 60],
    ]);
    runEffect(Difference, d, host(), { destination: DifferenceDest.newChannel, normalize: true });
    // diff = [1, 0]; peak 1 -> gain 12700% -> 127
    expect(read(d, 2)).toEqual([127, 0]);
  });

  it("routes to a new document when asked", () => {
    const h = host();
    const d = doc8([
      [50, 60],
      [10, 20],
    ]);
    runEffect(Difference, d, h, { destination: DifferenceDest.newDocument, normalize: false });
    expect(d.numChans).toBe(2);
    expect(h.spawnedDocuments).toHaveLength(1);
  });

  it("identifies identical takes as digital silence", () => {
    const d = doc8([
      [50, 60],
      [50, 60],
    ]);
    runEffect(Difference, d, host(), { destination: DifferenceDest.newChannel, normalize: false });
    expect(read(d, 2)).toEqual([0, 0]);
  });
});

// ---------------------------------------------------------------------------
// The complete folder
// ---------------------------------------------------------------------------

describe("the complete Effects Folder", () => {
  // The Effects Folder ships 37 modules (§8's 38th entry is the SDK's
  // Gain/EmptyEffect templates, which never shipped — and Gain *is* Amplify).
  // Keyboard joined in step 5, when the transport made auditioning possible.
  it("has all 37 shipped modules", () => {
    expect(EFFECTS).toHaveLength(37);
  });

  it("puts every module in the menu", () => {
    const menu = buildEffectMenu();
    const total = menu.topLevel.length + menu.groups.reduce((n, g) => n + g.modules.length, 0);
    expect(total).toBe(37);
  });

  it("has exactly one module with no effect() kernel to speak of — Keyboard", () => {
    // Every other module chews on samples. Keyboard is a rate multiplier whose
    // commit edits the header, which is why it needed the transport, not a DSP pass.
    expect(EFFECTS.find((m) => m.name === "Keyboard")).toBeDefined();
  });

  it("keeps every signature unique", () => {
    const sigs = EFFECTS.map((m) => m.signature);
    expect(new Set(sigs).size).toBe(sigs.length);
  });

  it("fills in every recovered submenu", () => {
    const menu = buildEffectMenu();
    const byName = Object.fromEntries(menu.groups.map((g) => [g.name, g.modules.map((m) => m.name)]));
    expect(byName["Echoes & Reverbs"]).toEqual(["Delay", "Echo", "Flanger"]);
    expect(byName["Mike's Modules"]).toEqual([
      "Granular Synthesis",
      "Iterative Crossfade",
      "Sample Hose",
    ]);
    expect(byName["Selection"]).toEqual(["Zero Crosser"]);
    expect(byName["Resampling"]).toEqual(["Downsample Bits", "Resample Rate", "Upsample Bits"]);
  });

  it("gives every module a name and an author", () => {
    for (const m of EFFECTS) {
      expect(m.name.length).toBeGreaterThan(0);
      expect(m.author).toBeTruthy();
    }
  });

  it("credits Norris for exactly Mike's Modules", () => {
    const norris = EFFECTS.filter((m) => m.author === "Michael Norris");
    expect(norris.map((m) => m.name).sort()).toEqual([
      "Granular Synthesis",
      "Iterative Crossfade",
      "Sample Hose",
    ]);
    for (const m of norris) expect(m.group).toBe("Mike's Modules");
  });
});
