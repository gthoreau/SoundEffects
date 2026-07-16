/**
 * **Granular Synthesis** — SPEC §8 #13.
 * `[BINARY: disassembly/Granular-Synthesis.md]` for the kernels;
 * **`[INTERPRETED]` for the grain scheduling.**
 *
 * Read this before trusting the output: the recovery covers the *pieces* — the
 * grain envelope (confirmed three ways: 68k build, 68881 build, and the Read Me's
 * published equation recovered as Expressionist source from `PICT 1001`), the
 * Curvature tween, and the ANSI-C `rand()` driving Variance. What it does **not**
 * cover is how grains are *scheduled*: the hop between them, how Density maps to
 * overlap, how the source cursor advances, and whether overlapping grains add or
 * replace. SPEC §8 has only "quasi-Gaussian grains stepped through the source"
 * `[DOC, formulas]`.
 *
 * So: every formula below is verified, and the loop that calls them is a
 * reasonable reading. The envelope and tween are independently testable and are
 * the parts worth trusting. See SPEC §14 item 21.
 *
 * Norris breaks the house style on purpose — the envelope is a **floating-point
 * per-sample gain**, then clamped, not Ricci's integer-only convention. That's why
 * this module shipped FPU and PowerPC builds.
 */

import { frameSizeFor, getSample, setSample, silentByte } from "../samples.js";
import { channelRange, expandEmptySelection } from "../selection.js";
import { AnsiCRandom } from "../runtime/rng.js";
import { kModCancel, kModNoError, type ModResult } from "../runtime/results.js";
import { ItemType, type SettingsSchema } from "../runtime/schema.js";
import { Unit } from "../units.js";
import { grainEnvelope, tween } from "./norris-shared.js";
import type { EffectModule } from "../runtime/module.js";

export interface GranularSettings {
  /** Bytes. */
  newLength: number;
  units: number;
  /** Bytes. */
  initialGrainLength: number;
  /** Bytes. */
  finalGrainLength: number;
  /** Bytes of jitter applied to grain length via the ANSI-C rand(). */
  variance: number;
  /** 100 = linear, 0 = convex, 200 = concave. */
  curvature: number;
  /** Percent. */
  initialDensity: number;
  finalDensity: number;
  densityCurvature: number;
  /** Percent. 0 = full envelope, 100 = no enveloping at all. */
  fadeLevel: number;
  /** Seeds the reproducible grain jitter. */
  seed?: number;
}

export const GranularSynthesis: EffectModule<GranularSettings> = {
  name: "Granular Synthesis",
  signature: "GranularSynthesis",
  group: "Mike's Modules",
  version: "1.0",
  author: "Michael Norris",
  progressText: "Granulating…",

  defaults: () => ({
    newLength: 222000,
    units: Unit.milliseconds,
    initialGrainLength: 1000,
    finalGrainLength: 1000,
    variance: 0,
    curvature: 100,
    initialDensity: 100,
    finalDensity: 100,
    densityCurvature: 100,
    fadeLevel: 0,
  }),

  schema: (): SettingsSchema => [
    { type: ItemType.editFieldUnitDependent, key: "newLength", label: "New Length:" },
    { type: ItemType.unitsPopup, key: "units" },
    { type: ItemType.editFieldUnitDependent, key: "initialGrainLength", label: "Initial Grain Length:" },
    { type: ItemType.editFieldUnitDependent, key: "finalGrainLength", label: "Final Grain Length:" },
    { type: ItemType.editFieldUnitDependent, key: "variance", label: "Variance:" },
    { type: ItemType.editFieldNumerical, key: "curvature", label: "Curvature:" },
    { type: ItemType.editFieldNumerical, key: "initialDensity", label: "Initial Density:" },
    { type: ItemType.editFieldNumerical, key: "finalDensity", label: "Final Density:" },
    { type: ItemType.editFieldNumerical, key: "densityCurvature", label: "Density Curvature:" },
    { type: ItemType.editFieldNumerical, key: "fadeLevel", label: "Fade Level:" },
  ],

  effect(doc, host, prefs): ModResult {
    expandEmptySelection(doc);
    host.setupProgress("Granulating…");

    const frameSize = frameSizeFor(doc.bps);
    const { min, max } = host.getSampleValueLimits(doc.bps);
    const rng = new AnsiCRandom(prefs.seed ?? 1);
    const f = prefs.fadeLevel / 100;

    const srcIdx = doc.firstSelCh - 1;
    const srcCh = doc.channels[srcIdx];
    if (!srcCh) {
      host.closeProgress();
      return kModCancel;
    }
    const range = channelRange(doc, srcIdx);
    const srcFrames = Math.floor((range.end - range.start) / frameSize);
    if (srcFrames < 1) {
      host.closeProgress();
      return kModCancel;
    }

    const outFrames = Math.floor(prefs.newLength / frameSize);
    if (outFrames < 1) {
      host.closeProgress();
      return kModCancel;
    }

    // Accumulate in floats: grains overlap, and the envelope is a float gain.
    // `[INTERPRETED]`: that overlapping grains ADD is the standard granular
    // reading and the only one under which Density > 100% means anything, but
    // the recovery shows only a per-grain "clamp and write back" and does not
    // settle add-vs-replace.
    const acc = new Float64Array(outFrames);

    let outPos = 0;
    let guard = 0;
    while (outPos < outFrames && guard++ < outFrames * 4) {
      // Grain length: the tween, jittered by Variance, truncated before use as a
      // byte count (FTINTX — the truncate-don't-round habit).
      let grainBytes = tween(
        outPos,
        outFrames,
        prefs.initialGrainLength,
        prefs.finalGrainLength,
        prefs.curvature,
      );
      if (prefs.variance > 0) {
        // rand() is 0..0x7FFF; centre it to ±variance.
        const jitter = (rng.next() / AnsiCRandom.RAND_MAX) * 2 - 1;
        grainBytes += jitter * prefs.variance;
      }
      const grainFrames = Math.max(1, Math.trunc(grainBytes / frameSize));

      // The source cursor steps through the selection proportionally, so the
      // output traverses the source once across its whole length.
      const srcStart = Math.trunc((outPos / outFrames) * srcFrames);

      for (let x = 0; x < grainFrames; x++) {
        const o = outPos + x;
        if (o >= outFrames) break;
        const s = srcStart + x;
        if (s >= srcFrames) break;
        const sample = getSample(srcCh.data, range.start + s * frameSize, doc.bps);
        // The verified kernel: a float gain from the grain envelope.
        acc[o]! += sample * grainEnvelope(x, grainFrames, f);
      }

      // Density -> hop. 100% is grains back-to-back; higher overlaps them.
      const density = Math.max(
        1,
        tween(outPos, outFrames, prefs.initialDensity, prefs.finalDensity, prefs.densityCurvature),
      );
      const hop = Math.max(1, Math.trunc((grainFrames * 100) / density));
      outPos += hop;
    }

    const out = new Uint8Array(outFrames * frameSize);
    out.fill(silentByte(doc.bps));
    for (let i = 0; i < outFrames; i++) {
      let v = Math.trunc(acc[i]!);
      if (v < min) v = min;
      else if (v > max) v = max;
      setSample(out, i * frameSize, v, doc.bps);
    }

    // Granulates INTO new channels.
    host.addNewChannel(doc);
    doc.channels[doc.numChans - 1]!.data = out;

    host.closeProgress();
    return kModNoError;
  },
};
