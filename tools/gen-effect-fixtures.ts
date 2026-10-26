/**
 * Golden-vector generator for the native effect ports.
 *
 * The TypeScript effects in `src/effects/` are the byte-accurate reference (§8):
 * they carry the disassembly's quirks on purpose — truncating integer divides,
 * `y(n-1)` seeded with the first sample, Smooth's 2-point average that excludes
 * the centre sample, Select Peak's skip-short-channels rule. None of that
 * survives a port done by eye.
 *
 * So: run every module here over deterministic inputs, dump input + params +
 * output as a binary fixture, and let `native/tests/` replay the same cases
 * against the C++ and demand byte-identical results. A port is then either green
 * or red — never "looks about right".
 *
 * Run:  npm run fixtures      (regenerates native/tests/fixtures/effects/)
 *
 * The output is COMMITTED. It is the contract between the two implementations,
 * and the native test must not need a Node toolchain to run.
 */

import { mkdirSync, writeFileSync, readdirSync, rmSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import { SoundDocument } from "../src/document.js";
import { Host } from "../src/runtime/host.js";
import type { EffectModule } from "../src/runtime/module.js";
import { DoExtend } from "../src/runtime/host.js";
import { doubleToFixed, Unit } from "../src/units.js";
import { getSample, setSample, frameSizeFor, silentByte } from "../src/samples.js";
import { leaveSilence, shiftSound } from "../src/edit.js";
import { selectExactly, selectPeak, selectSilence, type ExactlyAnchor } from "../src/selection.js";
import { EFFECTS } from "../src/effects/index.js";

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT_DIR = join(HERE, "..", "native", "tests", "fixtures", "effects");

/** Bump when the container layout changes; the native reader checks it. */
const FIXTURE_VERSION = 1;

// ---------------------------------------------------------------------------
// Deterministic input material
// ---------------------------------------------------------------------------

/**
 * A tiny LCG, used only to fill test buffers. Deliberately NOT one of the app's
 * two RNGs (the Toolbox `_Random` trap, and the ANSI-C LCG the Norris modules
 * use) — fixture content must never be confused with effect behaviour.
 */
function lcg(seed: number): () => number {
  let s = seed >>> 0;
  return () => {
    s = (Math.imul(s, 1664525) + 1013904223) >>> 0;
    return s;
  };
}

/**
 * Fill a channel with material that actually exercises the kernels: a swept sine
 * (zero-crossings for Weird Tempo and Zero Crosser), a passage of exact digital
 * silence (Select Silence's metric, and the "silence passes through unchanged"
 * paths), some near-full-scale peaks (the saturating clamp), and a noisy tail.
 */
function fillChannel(data: Uint8Array, bps: number, seed: number, scale: number): void {
  const frameSize = frameSizeFor(bps);
  const frames = Math.floor(data.length / frameSize);
  const peak = Math.pow(2, Math.min(frameSize, 4) * 8 - 1) - 1;
  const rand = lcg(seed);

  for (let i = 0; i < frames; i++) {
    const t = i / frames;
    let v: number;
    if (t < 0.4) {
      // Swept sine, scaled well below full scale.
      v = Math.trunc(Math.sin(i * (0.12 + t * 0.5)) * peak * 0.55 * scale);
    } else if (t < 0.55) {
      v = 0; // exact digital zero — the app's only definition of silence
    } else if (t < 0.7) {
      // Alternating near-full-scale, so anything with gain clips.
      const loud = Math.trunc(peak * 0.97 * scale);
      v = i % 2 === 0 ? loud : -loud;
    } else {
      v = Math.trunc(((rand() % (2 * peak + 1)) - peak) * scale);
    }
    setSample(data, i * frameSize, v, bps);
  }
}

interface DocShape {
  name: string;
  bps: number;
  /** One entry per track — its length in BYTES. Ragged on purpose. */
  sizes: number[];
  rateHz?: number;
  /**
   * The rate as a raw 16.16 Fixed, when the exact quantised value is the point
   * and `rateHz` would round to the wrong tick. Wins over `rateHz`.
   */
  rateFixed?: number;
  /**
   * Amplitude scale for the generated material. Normalize is the reason this
   * exists: on near-full-scale input its scale factor truncates to 1 and it
   * correctly does nothing, which tests none of its arithmetic.
   */
  scale?: number;
  /**
   * Per-track output routing, `[left, right]`. Defaults to L+R on every track.
   *
   * Quick Mix's stereo mode routes BY these toggles — the routing you hear while
   * editing is the routing you get when you mix (§3) — so a document where every
   * track is L+R cannot tell that behaviour from ignoring the toggles entirely.
   * Verified by mutation: it did not.
   */
  pans?: Array<[boolean, boolean]>;
  /**
   * Per-track 8.8 volume; 0x0100 is unity. "Use each channel's pan" scales by
   * this, and at unity that multiply is invisible — and worse, it keeps every
   * accumulator integral, which hides whether the mix truncates or rounds.
   */
  volumes?: number[];
}

/**
 * Fixed 0x56EE8BA3 = 22254 6/11 Hz — the Mac "Standard" rate, read straight from
 * Robotize's binary. See the shapes below for why the EXACT value is load-bearing.
 */
const NATIVE_RATE_FIXED = 0x56ee8ba3;

const SHAPES: Record<string, DocShape> = {
  mono8: { name: "mono8", bps: 8, sizes: [96] },
  mono16: { name: "mono16", bps: 16, sizes: [192] },
  // Ragged lengths are the point (§5): a selection past a short track must clamp
  // per track, and effects must shorten only what they touch.
  ragged8: { name: "ragged8", bps: 8, sizes: [96, 64, 128] },
  stereo16: { name: "stereo16", bps: 16, sizes: [192, 160] },

  // Robotize's comb delay is trunc(190 · rate / 22254.5454) ≈ 190 FRAMES at the
  // native rate, and §8 #27 says a selection shorter than that is a silent no-op.
  // Every short shape above is shorter than the delay, so Robotize needs its own.
  long8: { name: "long8", bps: 8, sizes: [768] },
  long16: { name: "long16", bps: 16, sizes: [1536] },
  longRagged8: { name: "longRagged8", bps: 8, sizes: [768, 512, 1024] },

  // Robotize's delay is trunc(190 × rate / 0x56EE8BA3), and that truncation is
  // sharper than it looks. AT the native rate the division cancels and D is 190 on
  // the nose; ONE 1/65536th BELOW it, D drops to 189. Only the second shape can
  // tell the exact Fixed constant from a decimal approximation of it — a port
  // using 22254.5454 gets 190 there and is wrong by a whole sample.
  nativeRate8: { name: "nativeRate8", bps: 8, sizes: [768], rateFixed: NATIVE_RATE_FIXED },
  belowNative8: { name: "belowNative8", bps: 8, sizes: [768], rateFixed: NATIVE_RATE_FIXED - 1 },

  // Quick Mix is the only module that reads a track's routing and volume, and
  // every other shape leaves both at their defaults. Three tracks, hard-panned
  // L / R / centre, at 1.5x, 0.5x and unity — so stereo routing is observable,
  // the volume multiply is observable, and the accumulator stops being integral.
  panned16: {
    name: "panned16",
    bps: 16,
    sizes: [192, 160, 176],
    pans: [
      [true, false],
      [false, true],
      [true, true],
    ],
    volumes: [0x0180, 0x0080, 0x0100],
  },

  // Flanger's envelope follower scans 1024-SAMPLE blocks and interpolates between
  // a block's peak and the reference peak. Every other shape is under one block, so
  // the two are the same number and the interpolation is invisible. Four blocks,
  // and fillChannel's four regions guarantee the block peaks genuinely differ.
  veryLong8: { name: "veryLong8", bps: 8, sizes: [4096] },

  // Two tracks, quiet enough that Difference's normalize gain has a remainder
  // past half its divisor — see the Difference cases for the arithmetic.
  quietStereo16: { name: "quietStereo16", bps: 16, sizes: [192, 160], scale: 0.35 },

  quiet8: { name: "quiet8", bps: 8, sizes: [96], scale: 0.12 },
  quiet16: { name: "quiet16", bps: 16, sizes: [192], scale: 0.08 },
  quietRagged8: { name: "quietRagged8", bps: 8, sizes: [96, 64, 128], scale: 0.15 },
};

function buildDoc(shape: DocShape): SoundDocument {
  const doc = new SoundDocument({
    name: shape.name,
    bps: shape.bps,
    rate: shape.rateFixed ?? doubleToFixed(shape.rateHz ?? 22254.5454),
    numChans: 0,
  });
  shape.sizes.forEach((size, i) => {
    const ch = doc.addChannel(doc.numChans + 1, size);
    fillChannel(ch.data, shape.bps, 0x1234 + i * 977, shape.scale ?? 1);
    const pan = shape.pans?.[i];
    if (pan) {
      ch.outLeft = pan[0];
      ch.outRight = pan[1];
    }
    const volume = shape.volumes?.[i];
    if (volume !== undefined) ch.volume = volume;
  });
  doc.firstSelCh = 1;
  doc.lastSelCh = doc.numChans;
  return doc;
}

/** How a case scopes the selection, mirroring what the panel will offer. */
type SelKind =
  | "caret"
  | "mid"
  | "firstTrack"
  | "secondTrack"
  | "pastShort"
  | "silenceEdge";

function applySelection(doc: SoundDocument, kind: SelKind): void {
  const frameSize = doc.frameSize;
  const max = doc.maxChSize();
  const snap = (b: number) => b - (b % frameSize);

  switch (kind) {
    case "caret":
      // Empty selection ⇒ whole channels (Gain.c). The panel's "window selected,
      // nothing selected inside" case resolves to exactly this.
      doc.selSt = 0;
      doc.selEnd = 0;
      doc.firstSelCh = 1;
      doc.lastSelCh = doc.numChans;
      break;
    case "mid":
      doc.selSt = snap(Math.floor(max * 0.25));
      doc.selEnd = snap(Math.floor(max * 0.75));
      doc.firstSelCh = 1;
      doc.lastSelCh = doc.numChans;
      break;
    case "firstTrack":
      doc.selSt = snap(Math.floor(max * 0.1));
      doc.selEnd = snap(Math.floor(max * 0.6));
      doc.firstSelCh = 1;
      doc.lastSelCh = 1;
      break;
    case "secondTrack":
      doc.selSt = snap(Math.floor(max * 0.1));
      doc.selEnd = snap(Math.floor(max * 0.6));
      doc.firstSelCh = Math.min(2, doc.numChans);
      doc.lastSelCh = Math.min(2, doc.numChans);
      break;
    case "pastShort":
      // Runs past the END of the short track — the per-track clamp is the whole
      // reason `channelRange` clamps before it frame-snaps.
      doc.selSt = snap(Math.floor(max * 0.4));
      doc.selEnd = max;
      doc.firstSelCh = 1;
      doc.lastSelCh = doc.numChans;
      break;
    case "silenceEdge": {
      // Sits inside the run of exact digital zeros and ends EXACTLY on the first
      // negative sample after it.
      //
      // Zero Crosser is why this exists. "Zero counts as positive" and the
      // crossing search's own `limit` are both invisible on ordinary material —
      // a sine sweep changes sign every few frames, so the search always finds
      // something long before the convention or the bound can matter. Only with
      // silence at the edge and the crossing sitting ON the boundary do they
      // become observable, which mutation testing is how we found out.
      //
      // Located by SCANNING rather than by fractions of the buffer: the sample
      // that matters is a property of the generated material, and a hard-coded
      // offset would silently stop meaning anything the day fillChannel changes.
      const data = doc.channels[0]!.data;
      const floor = Math.floor(data.length * 0.45);
      let firstNeg = 0;
      for (let p = floor; p + frameSize <= data.length; p += frameSize) {
        if (getSample(data, p, doc.bps) < 0) {
          firstNeg = p;
          break;
        }
      }
      doc.selSt = snap(Math.max(0, firstNeg - 12 * frameSize));
      doc.selEnd = firstNeg;
      doc.firstSelCh = 1;
      doc.lastSelCh = doc.numChans;
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// The case matrix
// ---------------------------------------------------------------------------

type Params = Record<string, unknown>;

interface CaseSpec {
  shape: keyof typeof SHAPES;
  sel: SelKind;
  params?: Params;
  seed?: number;
  /** Answer to AskDoExtend() when the pref is "ask". */
  extend?: boolean;
  /**
   * This case is EXPECTED to leave the document untouched, and that refusal is
   * the behaviour under test — Replicate with extension refused has nowhere to
   * put the copies, so it declines (`replicate.ts`). Undeclared no-ops are
   * reported, because a fixture the port passes by doing nothing is worthless.
   */
  expectNoOp?: boolean;
}

/** The matrix every module gets unless it overrides. */
const DEFAULT_CASES: CaseSpec[] = [
  { shape: "mono8", sel: "caret" },
  { shape: "mono16", sel: "mid" },
  { shape: "ragged8", sel: "pastShort" },
  { shape: "ragged8", sel: "secondTrack" },
  { shape: "stereo16", sel: "mid" },
];

/**
 * Per-module additions, keyed by `module.name` — the menu name, spaces and all
 * ("Fade In", not FadeIn). Keying these by the export identifier instead is a
 * silent failure: the override never matches, the module quietly runs on its
 * defaults, and a Fade of 0 ms writes a fixture that proves nothing. `generate()`
 * hard-fails on a name that matches no module, and on a param key the module's
 * defaults don't declare, so that mistake cannot be made quietly again.
 *
 * Three reasons a module lands here:
 *   1. its defaults are degenerate (Fade's 0 ms; Waveform Generator's 0 length),
 *   2. its defaults are enormous — the Norris modules render 222000 bytes of new
 *      audio (~10 s), and Echo/Delay's 3500-byte delay dwarfs a test buffer.
 *      These are fixture sizes, not behaviours: the kernels are identical at 2000
 *      bytes and the file stays diffable,
 *   3. it needs a specific shape — the two-track modules (Sum, Difference,
 *      Iterative Crossfade) are undefined on a mono document.
 */
const OVERRIDES: Record<string, CaseSpec[]> = {
  // Both extension answers, because grow-and-silence is the fiddly half. Delay
  // is in BYTES despite carrying a units popup, so it is scaled to the buffer.
  Echo: [
    { shape: "mono8", sel: "mid", params: { delay: 24, strength: 70 }, extend: false },
    { shape: "mono8", sel: "mid", params: { delay: 24, strength: 70 }, extend: true },
    { shape: "mono16", sel: "caret", params: { delay: 32, strength: 60 }, extend: true },
    { shape: "ragged8", sel: "pastShort", params: { delay: 16, strength: 80 }, extend: false },
    {
      shape: "mono16",
      sel: "mid",
      params: { delay: 24, strength: 70, onlyOn: true, onlyCount: 3 },
      extend: true,
    },
    // An ODD delay on a 16-bit shape. Every other case above is already
    // frame-aligned, so nothing tested the `delay - delay % frameSize` snap —
    // verified by mutation: dropping the alignment passed the whole suite.
    { shape: "mono16", sel: "mid", params: { delay: 25, strength: 70 }, extend: true },
    // Strength 99 is the only way to reach the 80-echo cap: at 70% the 8% decay
    // floor stops the sizing loop after 8 echoes and the cap is dead code. Here
    // amp falls ~1 per echo, so it would take ~92 — the cap bites first. Also
    // verified by mutation (80 -> 40 passed everything without this case).
    { shape: "mono8", sel: "mid", params: { delay: 8, strength: 99 }, extend: true },
  ],
  Delay: [
    { shape: "mono8", sel: "mid", params: { delay: 24, strength: 70 }, extend: false },
    { shape: "mono8", sel: "mid", params: { delay: 24, strength: 70 }, extend: true },
    { shape: "mono16", sel: "caret", params: { delay: 32, strength: 60 }, extend: true },
    { shape: "ragged8", sel: "pastShort", params: { delay: 16, strength: 80 }, extend: false },
  ],
  "Fade In": [
    { shape: "mono8", sel: "caret", params: { time: 48, units: Unit.bytes } },
    { shape: "mono16", sel: "mid", params: { time: 40, units: Unit.bytes } },
    { shape: "ragged8", sel: "pastShort", params: { time: 32, units: Unit.bytes } },
  ],
  "Fade Out": [
    { shape: "mono8", sel: "caret", params: { time: 48, units: Unit.bytes } },
    { shape: "mono16", sel: "mid", params: { time: 40, units: Unit.bytes } },
    { shape: "ragged8", sel: "pastShort", params: { time: 32, units: Unit.bytes } },
  ],
  Amplify: [
    { shape: "mono8", sel: "caret", params: { gain: 200 } },
    { shape: "mono8", sel: "mid", params: { gain: 50 } },
    // Deliberately clips, so the saturating clamp is exercised at both rails.
    { shape: "mono16", sel: "caret", params: { gain: 900 } },
    { shape: "ragged8", sel: "pastShort", params: { gain: 150 } },
    { shape: "ragged8", sel: "secondTrack", params: { gain: 33 } },
    { shape: "stereo16", sel: "mid", params: { gain: 250 } },
  ],
  // §8 #27: the comb delay is ~190 frames and a shorter selection is a silent
  // no-op, so this is the one module that needs a buffer longer than its delay.
  Robotize: [
    { shape: "long8", sel: "caret" },
    { shape: "long16", sel: "mid" },
    { shape: "longRagged8", sel: "pastShort" },
    { shape: "longRagged8", sel: "secondTrack" },
    // The delay-constant edge: D = 190 at the native rate, 189 one Fixed tick
    // below it. Without this pair a port can use a decimal approximation of
    // 0x56EE8BA3 and pass everything else — verified by mutation.
    { shape: "nativeRate8", sel: "caret" },
    { shape: "belowNative8", sel: "caret" },
  ],
  // Near-full-scale input makes Normalize's scale factor truncate to 1 — correct,
  // and a test of nothing. Quiet material is what exercises the arithmetic.
  Normalize: [
    { shape: "quiet8", sel: "caret" },
    { shape: "quiet16", sel: "mid" },
    { shape: "quietRagged8", sel: "pastShort" },
    { shape: "quietRagged8", sel: "secondTrack" },
    // Partial track coverage is the "Norm All / Just These" alert's scope.
    { shape: "quietRagged8", sel: "firstTrack", params: { scope: 2 } },
  ],
  // The IIR family quantises coefficients to one decimal and truncates: 0.55
  // must come back as 0.5. Feed it values that catch a port that rounds.
  "High Pass IIR": iirCases(),
  "Low Pass IIR": iirCases(),
  Shelving: iirCases(),
  Dither: [
    { shape: "mono8", sel: "caret", params: { mode: 1, percent: 10 }, seed: 1 },
    { shape: "mono16", sel: "caret", params: { mode: 1, percent: 10 }, seed: 1 },
    { shape: "mono16", sel: "mid", params: { mode: 2, points: 500 }, seed: 7 },
    { shape: "ragged8", sel: "pastShort", params: { mode: 1, percent: 25 }, seed: 42 },
  ],
  Noise: [
    { shape: "mono8", sel: "mid", seed: 1 },
    { shape: "mono16", sel: "mid", seed: 1 },
    // An odd-boundary range: Noise writes 16 bits at a time whatever the depth,
    // and fixes up the stray bytes at each end.
    { shape: "ragged8", sel: "firstTrack", seed: 99 },
    { shape: "stereo16", sel: "caret", seed: 3 },
  ],
  // Replicate extends the track in place, so with extension refused it has
  // nowhere to put the copies and declines outright — worth pinning down.
  Replicate: [
    { shape: "mono8", sel: "mid", params: { copies: 3, gapMode: 1 }, extend: true },
    {
      shape: "mono16",
      sel: "mid",
      params: { copies: 2, gapMode: 4, gapLength: 32, units: Unit.bytes },
      extend: true,
    },
    {
      shape: "ragged8",
      sel: "pastShort",
      params: { copies: 2, gapMode: 2, gapDivisor: 2 },
      extend: true,
    },
    {
      shape: "mono8",
      sel: "mid",
      params: { copies: 3, gapMode: 3, gapMultiplier: 2 },
      extend: true,
    },
    { shape: "mono8", sel: "mid", params: { copies: 3 }, extend: false, expectNoOp: true },
    // Divide-by-zero is a documented BEHAVIOUR — a friendly alert, then gap =
    // selection length — and nothing exercised it until mutation testing removed
    // the fallback and the suite stayed green.
    {
      shape: "mono8",
      sel: "mid",
      params: { copies: 2, gapMode: 2, gapDivisor: 0 },
      extend: true,
    },
    // An ODD gap on a 16-bit shape: every gap above is already frame-aligned, so
    // the `gap -= gap % frameSize` snap was untested.
    {
      shape: "mono16",
      sel: "mid",
      params: { copies: 2, gapMode: 4, gapLength: 25, units: Unit.bytes },
      extend: true,
    },
  ],
  // Two-track modules: a mono document is not a valid input for them.
  Sum: twoChannelCases(),
  Difference: [
    ...twoChannelCases(),
    // Normalize is off by default, so nothing exercised the scale factor at all —
    // and the whole point of the module is seeing how small the difference is.
    { shape: "stereo16", sel: "mid", params: { normalize: true } },
    // And the same thing on quiet material, because the gain is
    // trunc(max·100/peak) and whether that TRUNCATION is observable depends
    // entirely on where the peak lands. At full scale the remainder is 6212
    // against a half-peak of 31447 — nowhere near, so truncating and rounding
    // agree and the quirk goes untested. At 0.35 it is 18924 against 11006:
    // trunc gives 148, rounding 149. Computed, not guessed.
    { shape: "quietStereo16", sel: "mid", params: { normalize: true } },
    // Rendering to a New Document instead of a new track: the spawned-document
    // hand-back was untested for Difference.
    { shape: "stereo16", sel: "mid", params: { destination: 2 } },
  ],
  "Iterative Crossfade": [
    {
      shape: "stereo16",
      sel: "mid",
      params: { iterations: 4, silenceLength: 0, units: Unit.bytes },
    },
    {
      shape: "ragged8",
      sel: "mid",
      params: { iterations: 3, silenceLength: 16, units: Unit.bytes },
    },
    // Silence is given in BYTES and divided down to frames. ragged8 is 8-bit, so
    // that divide is a no-op there and the other case asks for no silence at all —
    // between them the conversion was untested.
    {
      shape: "stereo16",
      sel: "mid",
      params: { iterations: 3, silenceLength: 32, units: Unit.bytes },
    },
  ],
  "Quick Mix": [
    { shape: "stereo16", sel: "mid", params: { destination: 1, use: 1, usePan: false } },
    { shape: "stereo16", sel: "caret", params: { destination: 2, use: 2, usePan: true } },
    { shape: "ragged8", sel: "pastShort", params: { destination: 1, use: 1, usePan: false } },
    // The two cases above run on tracks that are all L+R at unity volume, which
    // makes stereo routing and the volume multiply invisible and keeps every
    // accumulator integral. Mutation testing confirmed all four of Quick Mix's
    // documented behaviours were untested until these landed.
    { shape: "panned16", sel: "mid", params: { destination: 1, use: 2, usePan: true } },
    { shape: "panned16", sel: "mid", params: { destination: 1, use: 1, usePan: true } },
  ],
  // Whole-document modules: a partial selection is a confirmation, not a scope.
  "Resample Rate": [
    { shape: "mono8", sel: "caret", params: { newRate: doubleToFixed(11025) } },
    { shape: "mono16", sel: "caret", params: { newRate: doubleToFixed(44100) } },
    { shape: "ragged8", sel: "caret", params: { newRate: doubleToFixed(8000) } },
  ],
  "Downsample Bits": [
    { shape: "mono16", sel: "caret", params: { newBps: 8, expert: false }, seed: 5 },
    // Expert + noise shaping injects RNG into the WHOLE document before the host
    // truncates — the one path where Downsample touches audio itself.
    {
      shape: "mono16",
      sel: "caret",
      params: { newBps: 8, expert: true, expertMode: 2 },
      seed: 5,
    },
    // 16 -> 8 keeps only the high byte, so a one-LSB difference in the injected
    // noise survives the truncation about once per 256 samples — on a 96-sample
    // shape, usually never. Dropping to 12 bits masks only the low 4, so the
    // same difference shows up 16x as often, and a long buffer makes it certain.
    // Without this, whether the injection truncates or rounds was untestable.
    {
      shape: "long16",
      sel: "caret",
      params: { newBps: 12, expert: true, expertMode: 2 },
      seed: 5,
    },
    // "Use Upsample Bits": asking to downsample to the current depth or higher is
    // declined outright, and that refusal was never pinned.
    { shape: "mono16", sel: "caret", params: { newBps: 16 }, expectNoOp: true },
  ],
  "Upsample Bits": [
    { shape: "mono8", sel: "caret", params: { newBps: 16 } },
    { shape: "ragged8", sel: "caret", params: { newBps: 16 } },
    // The nag, the other way round: upsampling to the current depth or lower.
    { shape: "mono16", sel: "caret", params: { newBps: 8 }, expectNoOp: true },
  ],
  "Waveform Generator": [
    { shape: "mono8", sel: "mid", params: { frequency: 440, amplitude: 80, waveType: 1 } },
    { shape: "mono16", sel: "mid", params: { frequency: 220, amplitude: 100, waveType: 2 } },
    { shape: "mono16", sel: "mid", params: { frequency: 110, amplitude: 60, waveType: 3 } },
    // Two holes in one case. At 220 Hz on a 48-frame selection the phase only
    // reaches 0.47, so it never wraps and the fmod that keeps square and sawtooth
    // periodic was doing nothing; 4000 Hz gives it 8 cycles to get wrong. And every
    // case above leaves Length at 0 (meaning "the whole selection"), so the clamp
    // of Length against the selection was never exercised either — 40 bytes of 96
    // means three fifths of the selection must come back untouched.
    {
      shape: "mono16",
      sel: "mid",
      params: { frequency: 4000, amplitude: 90, waveType: 2, length: 40, units: Unit.bytes },
    },
  ],
  "Zero Crosser": [
    { shape: "mono8", sel: "mid", params: { action: 1, side: 3 } },
    { shape: "mono16", sel: "mid", params: { action: 2, side: 3 } },
    { shape: "mono16", sel: "mid", params: { action: 1, side: 1 } },
    // "mid" rather than "pastShort": a selection already ending at the longest
    // track has no room to the right, so shrink-from-the-right finds nothing.
    { shape: "ragged8", sel: "mid", params: { action: 2, side: 2 } },
    // Expanding out of silence, where "zero counts as positive" decides the
    // answer on BOTH sides. Treat zero as negative instead and the left edge
    // lands on a different crossing and the right edge on the one after — a
    // difference no other case can see.
    { shape: "mono8", sel: "silenceEdge", params: { action: 1, side: 3 } },
    // Shrinking inward across a run of zeros that contains no crossing at all.
    // The search runs into its own `limit` — the only case that does — and finds
    // nothing, so the module declines and touches nothing. That refusal IS the
    // fixture: an off-by-one in the bound examines the frame AT selEnd, finds the
    // crossing sitting on it, and collapses the selection.
    {
      shape: "mono8",
      sel: "silenceEdge",
      params: { action: 2, side: 1 },
      expectNoOp: true,
    },
  ],
  // Period is in bytes too: the factory 22255 would not complete one LFO cycle
  // inside a test buffer, leaving the sweep — the whole effect — unexercised.
  Flanger: [
    { shape: "mono8", sel: "caret", params: { delay: 16, width: 8, period: 200 }, seed: 1 },
    { shape: "mono16", sel: "mid", params: { delay: 16, width: 8, period: 200 }, seed: 1 },
    {
      shape: "mono16",
      sel: "mid",
      params: { delay: 16, width: 8, period: 200, feedback: true },
      seed: 1,
    },
    // Modulation 2 and 3 are the triangular and sawtooth accumulators — a
    // different code path from the sinusoid, not a different constant.
    {
      shape: "mono16",
      sel: "mid",
      params: { delay: 16, width: 8, period: 200, modulation: 2 },
      seed: 1,
    },
    {
      shape: "mono16",
      sel: "mid",
      params: { delay: 16, width: 8, period: 200, modulation: 3 },
      seed: 1,
    },
    {
      shape: "stereo16",
      sel: "caret",
      params: {
        delay: 16,
        width: 8,
        period: 200,
        envelopeFollower: true,
        depth: 8,
        depthUnits: Unit.bytes,
      },
      seed: 1,
    },
    // Sawtooth HARD-RESETS where triangular reflects, but only once the
    // accumulator reaches Delay+Width. At period 200 on a 96-frame buffer it
    // climbs a fraction of the way and the reset never fires, so the two shapes
    // were indistinguishable. A short period on a long buffer wraps it repeatedly.
    {
      shape: "long16",
      sel: "caret",
      params: { delay: 16, width: 8, period: 64, modulation: 3 },
      seed: 1,
    },
    // The envelope follower, over a buffer longer than ONE 1024-sample block, with
    // "louder means shorter". Under one block the block peak and the reference peak
    // are the same number, so the interpolation between them cancels; and every
    // other case leaves louderMeans at Longer, so the subtraction branch never ran.
    {
      shape: "veryLong8",
      sel: "caret",
      params: {
        delay: 16,
        width: 8,
        period: 200,
        envelopeFollower: true,
        depth: 8,
        depthUnits: Unit.bytes,
        louderMeans: 2,
      },
      seed: 1,
    },
  ],
  "Granular Synthesis": [
    {
      shape: "mono8",
      sel: "caret",
      params: { newLength: 1024, units: Unit.bytes, initialGrainLength: 64, finalGrainLength: 64 },
      seed: 11,
    },
    // A grain-length and density sweep, so the tween's pow() actually runs with
    // a non-linear curvature.
    {
      shape: "mono16",
      sel: "mid",
      params: {
        newLength: 2048,
        units: Unit.bytes,
        initialGrainLength: 64,
        finalGrainLength: 192,
        curvature: 50,
        initialDensity: 100,
        finalDensity: 50,
        fadeLevel: 25,
        variance: 16,
      },
      seed: 11,
    },
    // Density ABOVE 100%, which is the only way grains overlap — and overlapping is
    // the whole reason the accumulator is a float buffer that adds rather than
    // replaces. Both cases above sit at 100% or below, so grains never touched and
    // add-vs-replace made no difference to a single byte.
    {
      shape: "mono16",
      sel: "mid",
      params: {
        newLength: 2048,
        units: Unit.bytes,
        initialGrainLength: 64,
        finalGrainLength: 64,
        initialDensity: 200,
        finalDensity: 200,
      },
      seed: 11,
    },
  ],
  "Sample Hose": [
    {
      shape: "mono8",
      sel: "caret",
      params: {
        newLength: 1024,
        units: Unit.bytes,
        "period.initialValue": 256,
        "period.finalValue": 256,
      },
      seed: 13,
    },
    // Rate ≠ 100 is the one linear-interpolation path in the whole app.
    {
      shape: "mono16",
      sel: "mid",
      params: {
        newLength: 2048,
        units: Unit.bytes,
        "rate.initialValue": 50,
        "rate.finalValue": 200,
        "rate.curvature": 50,
        "gain.initialValue": 100,
        "gain.finalValue": 40,
        "period.initialValue": 256,
        "period.finalValue": 384,
        "bite.initialValue": 64,
        "bite.finalValue": 64,
      },
      seed: 13,
    },
    // A Pan SWEEP across the stereo field. Both cases above leave Pan at its
    // factory 50 — dead centre — where left and right gains are both 0.5 and
    // inverting the mapping changes nothing at all.
    {
      shape: "mono16",
      sel: "mid",
      params: {
        newLength: 2048,
        units: Unit.bytes,
        "pan.initialValue": 0,
        "pan.finalValue": 100,
        "period.initialValue": 256,
        "period.finalValue": 256,
      },
      seed: 13,
    },
  ],
  Keyboard: [
    // Header-only: Change Pitch moves the declared rate and touches no audio.
    { shape: "mono8", sel: "caret", params: { semitones: 12 } },
    { shape: "mono16", sel: "caret", params: { semitones: -5 } },
    // Unison is a no-op — there is no pitch to change. Both cases above are
    // non-zero, so that early-out was untested.
    { shape: "mono8", sel: "caret", params: { semitones: 0 }, expectNoOp: true },
    // Down an octave, which HALVES the rate. The default rate's Fixed is odd
    // (1458473887), so the halved value lands on exactly .5 of a tick — the one
    // place where rounding to the nearest tick and truncating toward zero provably
    // disagree. Every other case here lands on a whole tick or close enough to
    // one that both give the same answer, which left "doubleToFixed ROUNDS" as an
    // untested claim. Deterministic, not lucky: if the material's rate ever
    // changes, check this case still straddles a half-tick.
    { shape: "mono8", sel: "caret", params: { semitones: -12 } },
  ],
};

function iirCases(): CaseSpec[] {
  return [
    { shape: "mono8", sel: "caret", params: { a: 0.5, b: 0.5 } },
    // 0.55 must truncate to 0.5, and 0.99 to 0.9 — a port that rounds fails here.
    { shape: "mono16", sel: "mid", params: { a: 0.55, b: 0.99 } },
    { shape: "mono16", sel: "caret", params: { a: 1, b: 0.5 } },
    { shape: "ragged8", sel: "pastShort", params: { a: 0.3, b: 0.7 } },
  ];
}

function twoChannelCases(): CaseSpec[] {
  return [
    { shape: "stereo16", sel: "mid" },
    { shape: "stereo16", sel: "caret" },
    { shape: "ragged8", sel: "mid" },
  ];
}

// ---------------------------------------------------------------------------
// Binary container
// ---------------------------------------------------------------------------

class Writer {
  private parts: Uint8Array[] = [];
  private len = 0;

  private push(a: Uint8Array): void {
    this.parts.push(a);
    this.len += a.length;
  }

  u8(v: number): void {
    this.push(Uint8Array.of(v & 0xff));
  }

  u32(v: number): void {
    const b = new Uint8Array(4);
    new DataView(b.buffer).setUint32(0, v >>> 0, true);
    this.push(b);
  }

  i32(v: number): void {
    const b = new Uint8Array(4);
    new DataView(b.buffer).setInt32(0, v | 0, true);
    this.push(b);
  }

  i64(v: number): void {
    const b = new Uint8Array(8);
    new DataView(b.buffer).setBigInt64(0, BigInt(Math.trunc(v)), true);
    this.push(b);
  }

  f64(v: number): void {
    const b = new Uint8Array(8);
    new DataView(b.buffer).setFloat64(0, v, true);
    this.push(b);
  }

  str(s: string): void {
    const bytes = new TextEncoder().encode(s);
    this.u32(bytes.length);
    this.push(bytes);
  }

  bytes(b: Uint8Array): void {
    this.push(b.slice());
  }

  finish(): Uint8Array {
    const out = new Uint8Array(this.len);
    let o = 0;
    for (const p of this.parts) {
      out.set(p, o);
      o += p.length;
    }
    return out;
  }
}

function writeDoc(w: Writer, doc: SoundDocument): void {
  w.str(doc.name);
  w.i64(doc.rate); // 16.16 Fixed — the native side divides by 65536 for its double
  w.i32(doc.bps);
  w.i32(doc.baseFreq);
  w.u32(doc.selSt);
  w.u32(doc.selEnd);
  w.i32(doc.firstSelCh);
  w.i32(doc.lastSelCh);
  w.u32(doc.loopSt);
  w.u32(doc.loopEnd);
  w.u32(doc.numChans);
  for (const ch of doc.channels) {
    w.u32(ch.data.length);
    w.u8(ch.outLeft ? 1 : 0);
    w.u8(ch.outRight ? 1 : 0);
    w.i32(ch.volume);
    w.bytes(ch.data);
  }
}

/**
 * Settings are written FLAT, with dotted keys — Sample Hose's five parameter
 * groups are nested objects (`rate.initialValue`), and a flat string-keyed map is
 * both what the native `Settings` type will be and what the schema-driven panel
 * indexes into. One shape, three consumers.
 */
function flatten(obj: Params, prefix = ""): Params {
  const out: Params = {};
  for (const [k, v] of Object.entries(obj)) {
    const key = prefix ? `${prefix}.${k}` : k;
    if (v !== null && typeof v === "object" && !Array.isArray(v)) {
      Object.assign(out, flatten(v as Params, key));
    } else {
      out[key] = v;
    }
  }
  return out;
}

/** Rebuild the nested shape the module's own settings struct expects. */
function unflatten(flat: Params): Params {
  const out: Params = {};
  for (const [key, v] of Object.entries(flat)) {
    const parts = key.split(".");
    let node = out;
    for (let i = 0; i < parts.length - 1; i++) {
      const p = parts[i]!;
      if (typeof node[p] !== "object" || node[p] === null) node[p] = {};
      node = node[p] as Params;
    }
    node[parts[parts.length - 1]!] = v;
  }
  return out;
}

/** Params are written typed, but the native reader coerces — a value that
    happens to be integral (IIR's a = 1) must still read back as a double. */
function writeParams(w: Writer, params: Params): void {
  const keys = Object.keys(params).sort(); // stable order, so diffs are readable
  w.u32(keys.length);
  for (const key of keys) {
    w.str(key);
    const v = params[key];
    if (typeof v === "boolean") {
      w.u8(0);
      w.i64(v ? 1 : 0);
    } else if (typeof v === "number") {
      if (Number.isInteger(v)) {
        w.u8(0);
        w.i64(v);
      } else {
        w.u8(1);
        w.f64(v);
      }
    } else {
      w.u8(2);
      w.str(String(v));
    }
  }
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

function slug(name: string): string {
  return name
    .replace(/[^A-Za-z0-9]+/g, "-")
    .replace(/^-|-$/g, "")
    .toLowerCase();
}

interface GeneratedCase {
  name: string;
  seed: number;
  extend: boolean;
  params: Params;
  input: SoundDocument;
  result: number;
  output: SoundDocument;
  spawned: SoundDocument[];
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function runCase(module: EffectModule<any>, spec: CaseSpec, index: number): GeneratedCase {
  const shape = SHAPES[spec.shape]!;
  const doc = buildDoc(shape);
  applySelection(doc, spec.sel);

  // Snapshot BEFORE the effect: it mutates in place, and the fixture needs both
  // sides. There is no undo (§1) — cloning here is the test's privilege, not the
  // app's.
  const input = doc.clone();

  // Merge FLAT, so a dotted override ("rate.initialValue") lands inside the
  // nested group rather than sitting beside it as a key nobody reads.
  const base = flatten(module.defaults ? module.defaults() : {});
  const overrides = spec.params ?? {};

  // A param key the module never declared is a typo, and a typo here is silent:
  // the merge succeeds, the module ignores the extra key, and the fixture records
  // default behaviour under an override's name. Modules with no defaults() take
  // no params at all, so any key is wrong there too.
  for (const key of Object.keys(overrides)) {
    if (!(key in base)) {
      throw new Error(
        `unknown param '${key}' (declared: ${Object.keys(base).join(", ") || "none"})`,
      );
    }
  }

  const flat = { ...base, ...overrides };
  const params = unflatten(flat);
  const validated = module.validate ? module.validate(params) : params;

  const seed = spec.seed ?? 1;
  const extend = spec.extend ?? false;
  const host = new Host({
    randSeed: seed,
    askDoExtend: () => extend,
    // Every sample, so a module that polls the flag takes its cancel-check path
    // deterministically. Nothing here ever cancels.
    progressInterval: 1,
  });
  host.doExtend = DoExtend.ask;

  const result = module.effect(doc, host, validated);

  return {
    name: `${spec.shape}-${spec.sel}-${index}`,
    seed,
    extend,
    params: flatten(validated as Params),
    input,
    result,
    output: doc,
    spawned: [...host.spawnedDocuments],
  };
}

/**
 * Did this case actually do anything? A fixture whose output equals its input is
 * a hole in the contract: the native port passes it by doing nothing at all.
 *
 * Compares the header too, not just the buffers — Keyboard's Change Pitch is a
 * legitimate audio-untouched effect that moves only the declared rate (§8 #18),
 * and it must not read as a no-op.
 */
function changedAnything(a: SoundDocument, b: SoundDocument, spawned: number): boolean {
  if (spawned > 0) return true;
  if (a.rate !== b.rate || a.bps !== b.bps || a.baseFreq !== b.baseFreq) return true;
  if (a.selSt !== b.selSt || a.selEnd !== b.selEnd) return true;
  if (a.firstSelCh !== b.firstSelCh || a.lastSelCh !== b.lastSelCh) return true;
  if (a.numChans !== b.numChans) return true;
  for (let i = 0; i < a.numChans; i++) {
    const x = a.channels[i]!.data;
    const y = b.channels[i]!.data;
    if (x.length !== y.length) return true;
    for (let p = 0; p < x.length; p++) if (x[p] !== y[p]) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Menu commands — the Edit and Selection menu's destructive and selection maths
// ---------------------------------------------------------------------------

/**
 * Not effects, but the same contract: input document + parameters → the exact
 * document the reference produced, selection included. They share the container
 * (signature "Edit") so the native runner replays them beside the modules; it
 * dispatches by name to `engine/Edit` instead of the effect registry.
 *
 * `result` is what the command returned, as an integer (true = 1), or 0.
 */
interface EditCase {
  shape: keyof typeof SHAPES;
  sel: SelKind;
  params: Params;
  /** Runs after the selection: lead-in silence, an odd track range, a shrunk track. */
  prep?: (doc: SoundDocument) => void;
  expectNoOp?: boolean;
}

interface EditCommand {
  name: string;
  run: (doc: SoundDocument, params: Params) => number;
  cases: EditCase[];
}

/** Prepend `frames` of exact silence to each listed track (1-based → frames). */
function leadIn(frames: Record<number, number>): (doc: SoundDocument) => void {
  return (doc) => {
    for (const [track, n] of Object.entries(frames)) {
      const ch = doc.channels[Number(track) - 1]!;
      const pad = new Uint8Array(n * doc.frameSize);
      pad.fill(silentByte(doc.bps));
      const out = new Uint8Array(pad.length + ch.data.length);
      out.set(pad, 0);
      out.set(ch.data, pad.length);
      ch.data = out;
    }
  };
}

/** Make a track silent from end to end. */
function silenceTrack(track: number): (doc: SoundDocument) => void {
  return (doc) => doc.channels[track - 1]!.data.fill(silentByte(doc.bps));
}

/** A caret at `at` bytes (frame-snapped), on tracks first..last. */
function caretAt(at: number, first?: number, last?: number): (doc: SoundDocument) => void {
  return (doc) => {
    const b = at - (at % doc.frameSize);
    doc.selSt = b;
    doc.selEnd = b;
    if (first !== undefined) doc.firstSelCh = first;
    if (last !== undefined) doc.lastSelCh = last;
  };
}

const ANCHORS: ExactlyAnchor[] = ["before", "centered", "after"];

function both(...fs: Array<(doc: SoundDocument) => void>): (doc: SoundDocument) => void {
  return (doc) => fs.forEach((f) => f(doc));
}

const EDITS: EditCommand[] = [
  {
    name: "Leave Silence",
    run: (doc, p) => {
      leaveSilence(doc, Number(p.bytes), p.sound ? "sound" : "selectedChannels");
      return 0;
    },
    cases: [
      // Trim: a long lead-in comes down to N.
      { shape: "mono8", sel: "caret", params: { bytes: 4, sound: 1 }, prep: leadIn({ 1: 20 }) },
      // Pad: the material starts abruptly (fillChannel's first frame is a zero, so
      // the run is one frame), and 16-bit frames are two bytes.
      { shape: "mono16", sel: "mid", params: { bytes: 40, sound: 1 } },
      // Ragged lead-ins: the SHORTEST run is the one adjusted, the others keep their
      // extra silence; the selection moves by the difference.
      { shape: "ragged8", sel: "mid", params: { bytes: 6, sound: 1 }, prep: leadIn({ 1: 10, 2: 30, 3: 18 }) },
      // A track silent from end to end becomes exactly N.
      { shape: "ragged8", sel: "pastShort", params: { bytes: 12, sound: 1 }, prep: both(silenceTrack(2), leadIn({ 1: 3, 3: 7 })) },
      // The selected channels only (track 2 of 3), the others untouched.
      { shape: "ragged8", sel: "secondTrack", params: { bytes: 2, sound: 0 }, prep: leadIn({ 1: 9, 2: 9, 3: 9 }) },
      // "the Sound" re-selects every track before anything else.
      { shape: "stereo16", sel: "secondTrack", params: { bytes: 8, sound: 1 }, prep: leadIn({ 1: 12, 2: 5 }) },
      // Nothing audible anywhere: the run counts as 0 and the caret goes to N.
      { shape: "stereo16", sel: "mid", params: { bytes: 6, sound: 1 }, prep: both(silenceTrack(1), silenceTrack(2)) },
    ],
  },
  {
    name: "Shift Sound",
    run: (doc, p) => (shiftSound(doc, Number(p.bytes), p.right ? "right" : "left") ? 1 : 0),
    cases: [
      // The whole sound (caret, every track): right grows it. Left pads every track by
      // the shift, then moves the range back over the padding: a net no-op, which is
      // what 0.9.2 does (CODE 12 @0x1042), and the case pins it.
      { shape: "mono8", sel: "caret", params: { bytes: 10, right: 1 } },
      { shape: "ragged8", sel: "caret", params: { bytes: 7, right: 0 }, expectNoOp: true },
      // ...but on channels 2 to 3 only, the padding lands on track 1 too and is NOT
      // moved back there: track 1 ends up later than the others.
      { shape: "ragged8", sel: "caret", params: { bytes: 7, right: 0 }, prep: (d) => { d.firstSelCh = 2; } },
      // The selection, both ways, on 16-bit frames.
      { shape: "mono16", sel: "mid", params: { bytes: 20, right: 1 } },
      { shape: "mono16", sel: "mid", params: { bytes: 20, right: 0 } },
      // A shift longer than the range silences the whole old range.
      { shape: "stereo16", sel: "mid", params: { bytes: 120, right: 1 } },
      // Left past the start on one track pads EVERY track.
      { shape: "ragged8", sel: "secondTrack", params: { bytes: 30, right: 0 } },
      // A range running past the short track's end: left shortens it, right lengthens.
      { shape: "ragged8", sel: "pastShort", params: { bytes: 16, right: 0 } },
      { shape: "ragged8", sel: "pastShort", params: { bytes: 16, right: 1 } },
      // Channels 2 to 3, caret: their own longest end is the range.
      { shape: "ragged8", sel: "caret", params: { bytes: 5, right: 1 }, prep: (d) => { d.firstSelCh = 2; } },
      // Zero does nothing.
      { shape: "mono8", sel: "mid", params: { bytes: 0, right: 1 }, expectNoOp: true },
    ],
  },
  {
    name: "Select Exactly",
    run: (doc, p) => {
      selectExactly(doc, Number(p.bytes), ANCHORS[Number(p.anchor) - 1]!);
      return 0;
    },
    cases: [
      // After (the dialog's default), Centered on and Before around a selection.
      { shape: "mono8", sel: "mid", params: { bytes: 10, anchor: 3 } },
      { shape: "mono16", sel: "mid", params: { bytes: 30, anchor: 2 } },
      { shape: "mono16", sel: "mid", params: { bytes: 30, anchor: 1 } },
      // Clamped: Before at 0, After at the SELECTED tracks' end (track 2 is shorter).
      { shape: "ragged8", sel: "secondTrack", params: { bytes: 40, anchor: 1 } },
      { shape: "ragged8", sel: "secondTrack", params: { bytes: 40, anchor: 3 } },
      // Centered on a caret near the start: the start clamps, the end does not move back.
      { shape: "stereo16", sel: "caret", params: { bytes: 41, anchor: 2 }, prep: caretAt(10) },
      // An odd byte count on 16-bit: N/2 and the ends snap down to frames.
      { shape: "stereo16", sel: "mid", params: { bytes: 33, anchor: 2 } },
    ],
  },
  {
    name: "Select Peak",
    run: (doc, p) => {
      selectPeak(doc, { scope: p.sound ? "sound" : "selection", marginBytes: Number(p.margin) });
      return 0;
    },
    cases: [
      { shape: "mono8", sel: "caret", params: { margin: 0, sound: 1 } },
      { shape: "mono16", sel: "mid", params: { margin: 6, sound: 0 } },
      // Ragged: short tracks are skipped past their end, the sum is across tracks.
      { shape: "ragged8", sel: "pastShort", params: { margin: 5, sound: 0 } },
      { shape: "ragged8", sel: "secondTrack", params: { margin: 0, sound: 1 } },
      // A margin wider than the room on either side clamps each end on its own.
      { shape: "stereo16", sel: "firstTrack", params: { margin: 400, sound: 0 } },
      // A caret with "the Selection": the selected tracks' whole length is searched.
      { shape: "quietRagged8", sel: "caret", params: { margin: 3, sound: 0 }, prep: caretAt(20, 2, 3) },
    ],
  },
  {
    name: "Select Silence",
    run: (doc) => (selectSilence(doc) ? 1 : 0),
    cases: [
      // Inside fillChannel's run of exact zeros (40-55 % of each track).
      { shape: "mono8", sel: "caret", params: {}, prep: caretAt(45) },
      { shape: "mono16", sel: "caret", params: {}, prep: caretAt(90) },
      // Three tracks of different lengths, a shared stretch of silence laid over their
      // own: the per-track runs (each with its own margins) are intersected.
      { shape: "ragged8", sel: "caret", params: {}, prep: both((d) => { for (const ch of d.channels) ch.data.fill(0x80, 40, 60); }, caretAt(50)) },
      { shape: "stereo16", sel: "caret", params: {}, prep: caretAt(84) },
      // Silence running to a track's end takes no margin (Leave Silence's lead-in fixture
      // shape, reversed: a silent tail).
      { shape: "mono8", sel: "caret", params: {}, prep: both((d) => { d.channels[0]!.data.fill(0x80, 60); }, caretAt(70)) },
      // A real selection: insertion point required, nothing happens.
      { shape: "mono8", sel: "mid", params: {}, expectNoOp: true },
      // A caret on sound, not silence: no-op.
      { shape: "mono8", sel: "caret", params: {}, prep: caretAt(10), expectNoOp: true },
    ],
  },
];

/** Run every edit command's cases and write one fixture per command. */
function generateEdits(index: string[], problems: string[]): number {
  let total = 0;
  for (const command of EDITS) {
    const cases: GeneratedCase[] = [];
    command.cases.forEach((spec, i) => {
      const doc = buildDoc(SHAPES[spec.shape]!);
      applySelection(doc, spec.sel);
      spec.prep?.(doc);
      const input = doc.clone();
      const result = command.run(doc, spec.params);
      const c: GeneratedCase = {
        name: `${spec.shape}-${spec.sel}-${i}`,
        seed: 1,
        extend: false,
        params: spec.params,
        input,
        result,
        output: doc,
        spawned: [],
      };
      const changed = changedAnything(input, doc, 0);
      if (!changed && !spec.expectNoOp) problems.push(`  no-op   ${command.name} [${c.name}]`);
      else if (changed && spec.expectNoOp) problems.push(`  CHANGED ${command.name} [${c.name}]: expectNoOp, but it did something`);
      cases.push(c);
    });

    const w = new Writer();
    w.bytes(new TextEncoder().encode("SEFX"));
    w.u32(FIXTURE_VERSION);
    w.str(command.name);
    w.str("Edit");
    w.u32(cases.length);
    for (const c of cases) {
      w.str(c.name);
      w.u32(c.seed);
      w.u8(2);
      w.u8(0);
      writeParams(w, c.params);
      writeDoc(w, c.input);
      w.i32(c.result);
      writeDoc(w, c.output);
      w.u32(0);
    }
    const file = `edit-${slug(command.name)}.fx`;
    writeFileSync(join(OUT_DIR, file), w.finish());
    index.push(`${file}  ${command.name} (${cases.length} cases)`);
    total += cases.length;
  }
  return total;
}

function generate(): void {
  mkdirSync(OUT_DIR, { recursive: true });
  // Stale fixtures are worse than none: a renamed module would otherwise leave a
  // file the native test keeps passing against.
  for (const f of readdirSync(OUT_DIR)) {
    if (f.endsWith(".fx")) rmSync(join(OUT_DIR, f));
  }

  // An override keyed by a name no module answers to is dead config, and it fails
  // exactly the way a missing override does — silently, on defaults.
  const known = new Set(EFFECTS.map((m) => m.name));
  const orphans = Object.keys(OVERRIDES).filter((n) => !known.has(n));
  if (orphans.length > 0) {
    throw new Error(
      `OVERRIDES names match no module: ${orphans.join(", ")}\n` +
        `(they are keyed by module.name — "Fade In", not FadeIn)`,
    );
  }

  let totalCases = 0;
  let noOps = 0;
  const index: string[] = [];
  const problems: string[] = [];

  for (const module of EFFECTS) {
    const specs = OVERRIDES[module.name] ?? DEFAULT_CASES;
    const cases: GeneratedCase[] = [];

    specs.forEach((spec, i) => {
      try {
        const c = runCase(module, spec, i);
        const changed = changedAnything(c.input, c.output, c.spawned.length);
        if (!changed && !spec.expectNoOp) {
          noOps++;
          problems.push(`  no-op   ${module.name} [${c.name}]`);
        } else if (changed && spec.expectNoOp) {
          problems.push(`  CHANGED ${module.name} [${c.name}]: expectNoOp, but it did something`);
        }
        cases.push(c);
      } catch (err) {
        // A module that throws on a shape it cannot handle is telling us the
        // case is wrong, not that the module is broken. Say so loudly and skip:
        // a silently-missing case is a hole in the contract.
        problems.push(`  THREW   ${module.name} [${spec.shape}/${spec.sel}]: ${(err as Error).message}`);
      }
    });

    if (cases.length === 0) {
      problems.push(`  EMPTY   ${module.name}: no cases generated`);
      continue;
    }

    const w = new Writer();
    w.bytes(new TextEncoder().encode("SEFX"));
    w.u32(FIXTURE_VERSION);
    w.str(module.name);
    w.str(module.signature);
    w.u32(cases.length);

    for (const c of cases) {
      w.str(c.name);
      w.u32(c.seed);
      w.u8(2); // doExtend pref = "ask each time"
      w.u8(c.extend ? 1 : 0);
      writeParams(w, c.params);
      writeDoc(w, c.input);
      w.i32(c.result);
      writeDoc(w, c.output);
      w.u32(c.spawned.length);
      for (const s of c.spawned) writeDoc(w, s);
    }

    const file = `${slug(module.name)}.fx`;
    writeFileSync(join(OUT_DIR, file), w.finish());
    index.push(`${file}  ${module.name} (${cases.length} cases)`);
    totalCases += cases.length;
  }

  const editCases = generateEdits(index, problems);
  writeFileSync(join(OUT_DIR, "INDEX.txt"), index.sort().join("\n") + "\n");
  console.log(`${EFFECTS.length} modules, ${totalCases} cases, ${EDITS.length} menu commands, ${editCases} cases → ${OUT_DIR}`);

  if (problems.length > 0) {
    console.log(`\n${problems.length} to look at (${noOps} no-op):`);
    console.log(problems.join("\n"));
  }
}

generate();
