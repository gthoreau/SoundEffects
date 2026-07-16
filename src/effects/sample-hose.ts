/**
 * **Sample Hose** — SPEC §8 #28.
 * `[BINARY: disassembly/Sample-Hose-Iterative-Crossfade.md]` for the kernels;
 * **`[INTERPRETED]` for the spray scheduling.**
 *
 * A texture generator inspired by Fractal Design Painter's Image Hose: it sprays
 * copies of the selection (or random "bites" of it) into new channels. Five
 * parameter types × five sub-params each = the famous wall of 27 numeric fields,
 * which is worth reproducing faithfully in the UI.
 *
 * **The headline: Rate uses real linear interpolation.**
 *
 *     pos  = pos · (Rate/100)
 *     idx  = trunc(pos);  frac = pos − idx
 *     out  = src[idx] + frac·(src[idx+1] − src[idx])
 *     out *= gain
 *
 * This is **the only interpolating DSP anywhere in the app**. Every Ricci module
 * (Flanger, Echo, Delay) and the host's own resampler compute a fractional
 * position and then throw the fraction away with a truncate. Norris — a
 * third-party author — did it properly, which is why Sample Hose's pitch shifts
 * are smooth where Resample Rate's are aliased stairsteps.
 *
 * As with Granular Synthesis: the kernels here are verified, the *scheduling* is
 * not. The recovery is explicit that the offset→parameter mapping beyond Pan
 * (50 = centre) and the curvature group is "inferred from the defaults and the
 * DITL, not proven". See SPEC §14 item 21.
 */

import { frameSizeFor, getSample, setSample, silentByte } from "../samples.js";
import { channelRange, expandEmptySelection } from "../selection.js";
import { AnsiCRandom } from "../runtime/rng.js";
import { kModCancel, kModNoError, type ModResult } from "../runtime/results.js";
import { ItemType, type SettingsSchema } from "../runtime/schema.js";
import { Unit } from "../units.js";
import { tween } from "./norris-shared.js";
import type { EffectModule } from "../runtime/module.js";

/** One of the five parameter types: Initial/Final Value, their Variances, Curvature. */
export interface HoseParam {
  initialValue: number;
  initialVariance: number;
  finalValue: number;
  finalVariance: number;
  /** 100 = linear. */
  curvature: number;
}

export interface SampleHoseSettings {
  /** Bytes. Default 222000 ≈ 10 s at 22254.5454 Hz. */
  newLength: number;
  units: number;
  /** % pitch: 50 = octave down, 200 = octave up. */
  rate: HoseParam;
  /** %. */
  gain: HoseParam;
  /** 0 = L, 50 = C, 100 = R. Needs `stereo`. */
  pan: HoseParam;
  /** Subsection length in bytes; 0 = the whole sample. */
  bite: HoseParam;
  /** Time between copies, in bytes. */
  period: HoseParam;
  stereo: boolean;
  seed?: number;
}

const param = (initial: number, final: number): HoseParam => ({
  initialValue: initial,
  initialVariance: 0,
  finalValue: final,
  finalVariance: 0,
  curvature: 100,
});

/** Value of a parameter at output position `n`, with its variance applied. */
function paramAt(p: HoseParam, n: number, length: number, rng: AnsiCRandom): number {
  const base = tween(n, length, p.initialValue, p.finalValue, p.curvature);
  const variance = tween(n, length, p.initialVariance, p.finalVariance, p.curvature);
  if (variance === 0) return base;
  const jitter = (rng.next() / AnsiCRandom.RAND_MAX) * 2 - 1;
  return base + jitter * variance;
}

const hoseParamSchema = (key: string, label: string): SettingsSchema => [
  { type: ItemType.ignore, label },
  { type: ItemType.editFieldNumDecimal, key: `${key}.initialValue`, label: "Initial Value" },
  { type: ItemType.editFieldNumDecimal, key: `${key}.initialVariance`, label: "Initial Variance" },
  { type: ItemType.editFieldNumDecimal, key: `${key}.finalValue`, label: "Final Value" },
  { type: ItemType.editFieldNumDecimal, key: `${key}.finalVariance`, label: "Final Variance" },
  { type: ItemType.editFieldNumDecimal, key: `${key}.curvature`, label: "Curvature %" },
];

export const SampleHose: EffectModule<SampleHoseSettings> = {
  name: "Sample Hose",
  signature: "SampleHose",
  group: "Mike's Modules",
  version: "1.0.3",
  author: "Michael Norris",
  progressText: "Hosing…",

  /** The 106-byte settings handle's factory defaults, read from the binary. */
  defaults: () => ({
    newLength: 222000,
    units: Unit.milliseconds,
    rate: param(100, 100),
    gain: param(100, 100),
    pan: param(50, 50), // centre
    bite: param(0, 0), // 0 = whole sample
    period: param(10000, 10000),
    stereo: true,
  }),

  schema: (): SettingsSchema => [
    { type: ItemType.editFieldUnitDependent, key: "newLength", label: "New Length:" },
    { type: ItemType.unitsPopup, key: "units" },
    ...hoseParamSchema("rate", "Rate"),
    ...hoseParamSchema("gain", "Gain"),
    ...hoseParamSchema("pan", "Pan"),
    ...hoseParamSchema("bite", "Bite"),
    ...hoseParamSchema("period", "Period"),
    { type: ItemType.checkBox, key: "stereo", label: "Stereo" },
  ],

  effect(doc, host, prefs): ModResult {
    expandEmptySelection(doc);
    host.setupProgress("Hosing…");

    const frameSize = frameSizeFor(doc.bps);
    const { min, max } = host.getSampleValueLimits(doc.bps);
    const rng = new AnsiCRandom(prefs.seed ?? 1);

    const srcIdx = doc.firstSelCh - 1;
    const srcCh = doc.channels[srcIdx];
    if (!srcCh) {
      host.closeProgress();
      return kModCancel;
    }
    const range = channelRange(doc, srcIdx);
    const srcFrames = Math.floor((range.end - range.start) / frameSize);
    const outFrames = Math.floor(prefs.newLength / frameSize);
    if (srcFrames < 2 || outFrames < 1) {
      host.closeProgress();
      return kModCancel;
    }

    const readSrc = (i: number): number =>
      i >= 0 && i < srcFrames ? getSample(srcCh.data, range.start + i * frameSize, doc.bps) : 0;

    // Stereo sprays into two channels, routed by Pan; mono into one.
    const chans = prefs.stereo ? 2 : 1;
    const acc = Array.from({ length: chans }, () => new Float64Array(outFrames));

    let outPos = 0;
    let guard = 0;
    while (outPos < outFrames && guard++ < outFrames * 4) {
      const rateFactor = paramAt(prefs.rate, outPos, outFrames, rng) / 100;
      const gain = paramAt(prefs.gain, outPos, outFrames, rng) / 100;
      const panPct = Math.max(0, Math.min(100, paramAt(prefs.pan, outPos, outFrames, rng)));
      const biteBytes = paramAt(prefs.bite, outPos, outFrames, rng);
      const periodBytes = paramAt(prefs.period, outPos, outFrames, rng);

      // Bite 0 = the whole sample; otherwise a random subsection of that length.
      const biteFrames =
        biteBytes > 0 ? Math.max(1, Math.trunc(biteBytes / frameSize)) : srcFrames;
      const biteStart =
        biteBytes > 0 && srcFrames > biteFrames
          ? Math.trunc((rng.next() / AnsiCRandom.RAND_MAX) * (srcFrames - biteFrames))
          : 0;

      // Pan: 0 = L, 50 = C, 100 = R.
      const rightGain = prefs.stereo ? panPct / 100 : 1;
      const leftGain = prefs.stereo ? 1 - rightGain : 1;

      // Spray one copy, resampled by Rate with real linear interpolation.
      let pos = 0;
      for (let o = outPos; o < outFrames; o++) {
        const idx = Math.trunc(pos);
        if (idx >= biteFrames - 1) break;
        const frac = pos - idx;
        const a = readSrc(biteStart + idx);
        const b = readSrc(biteStart + idx + 1);
        // THE interpolation. Not a drop-sample read.
        const v = (a + frac * (b - a)) * gain;
        if (chans === 2) {
          acc[0]![o]! += v * leftGain;
          acc[1]![o]! += v * rightGain;
        } else {
          acc[0]![o]! += v;
        }
        pos += rateFactor;
      }

      const hop = Math.max(1, Math.trunc(periodBytes / frameSize));
      outPos += hop;
    }

    for (const a of acc) {
      const out = new Uint8Array(outFrames * frameSize);
      out.fill(silentByte(doc.bps));
      for (let i = 0; i < outFrames; i++) {
        let v = Math.trunc(a[i]!);
        if (v < min) v = min;
        else if (v > max) v = max;
        setSample(out, i * frameSize, v, doc.bps);
      }
      host.addNewChannel(doc);
      doc.channels[doc.numChans - 1]!.data = out;
    }

    host.closeProgress();
    return kModNoError;
  },
};
