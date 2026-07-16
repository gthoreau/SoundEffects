/**
 * **Smooth** — SPEC §8 #32. `[BINARY: disassembly/Smooth.md]`
 *
 * The cautionary tale of this whole project: the About box says "averaging every
 * sample with the preceding and the following one", which reads like a 3-point
 * `(prev+cur+next)/3`. The binary says otherwise —
 *
 *     out[i] = (in[i−1] + in[i+1]) / 2
 *
 * a **2-point average that excludes the centre sample entirely**. The current
 * sample plays no part in its own replacement. Docs can lie; this is why the
 * disassembly pass happened.
 */

import { frameSizeFor, getSample, setSample } from "../samples.js";
import { runSkeleton } from "../runtime/skeleton.js";
import { kModNoError } from "../runtime/results.js";
import type { EffectModule } from "../runtime/module.js";

export const Smooth: EffectModule = {
  name: "Smooth",
  signature: "Smooth",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Smoothing…",
  strings: ["The Smooth effect does not have any options to set."],

  effect(doc, host) {
    const frameSize = frameSizeFor(doc.bps);

    return runSkeleton(doc, host, { text: "Smoothing…", canCancel: false }, (pass, progress) => {
      // At least two frames, or the loop body never runs. (A real smooth needs
      // three: with exactly two, both samples are boundary samples.)
      if (!(pass.end - frameSize > pass.start)) return;

      // Seed: the first sample's ORIGINAL value, read once before the loop. It
      // is never itself overwritten.
      let prev = getSample(pass.data, pass.start, doc.bps);

      // Write cursor starts at the second sample and stops before the last —
      // the two boundary samples are excluded from the loop entirely, rather
      // than edge-clamped or padded the way most smoothers would.
      for (let p = pass.start + frameSize; p < pass.end - frameSize; p += frameSize) {
        const next = getSample(pass.data, p + frameSize, doc.bps);

        // Floor, NOT truncate-toward-zero. The original averages via a *logical*
        // shift on an offset-binary (8-bit) or XOR-sign-flipped excess (9–32-bit)
        // representation, both of which floor. So (-1 + 0)/2 is -1 here, where
        // C's signed `/2` would give 0. An easy detail to lose, and audible on
        // quiet material.
        const avg = Math.floor((prev + next) / 2);

        // Capture this position's ORIGINAL value before overwriting it, so the
        // next iteration's "prev" is a pristine input sample. Non-cascading:
        // every average uses two original neighbours, never a smoothed one.
        prev = getSample(pass.data, p, doc.bps);

        setSample(pass.data, p, avg, doc.bps);
        if (progress.tick()) return kModNoError;
      }
      // No clamp: the average of two in-range samples is always in range.
      // Length and selection are never touched — a pure fixed-length transform.
    });
  },
};
