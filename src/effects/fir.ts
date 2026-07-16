/**
 * The four "FIR" filters — SPEC §8 #2/#3/#14/#19. **`[DOC]`, not byte-verified.**
 *
 *     Low Pass FIR   y(n) = [x(n) + x(n−1)] / 2
 *     High Pass FIR  y(n) = [x(n) − x(n−1)] / 2
 *     Bandpass FIR   y(n) = x(n) − x(n−2) − 0.25·y(n−2)
 *     Bandstop FIR   y(n) = [x(n) + x(n−2) + 0.25·y(n−2)] / 2
 *
 * Two caveats worth carrying:
 *
 *   1. **Bandpass and Bandstop are misnomers** — both have a recursive `y(n−2)`
 *      term, so they are not FIR at all. The names ship as-is (§8 #2).
 *   2. These are `[DOC]` formulas. The three IIR filters were also "published
 *      one-liners" until the disassembly found 0.1 coefficient quantisation,
 *      per-term truncation and first-sample seeding hiding underneath. The same
 *      unwritten details almost certainly apply here — in particular the `0.25`
 *      is very unlikely to be floating-point in a module that ships no FPU build.
 *      Treat the edge behaviour below as a guess; see SPEC §14.
 */

import { frameSizeFor, getSample, setSample } from "../samples.js";
import { runSkeleton } from "../runtime/skeleton.js";
import { kModNoError } from "../runtime/results.js";
import type { EffectModule } from "../runtime/module.js";

const NO_OPTIONS = "Sorry, man. I haven't got any options.";

/**
 * One-tap filters. `[DOC]`
 *
 * We follow the house style the disassembly established everywhere else: seed
 * the delay from the first sample, leave that first sample untouched, and floor
 * the halving (as Smooth provably does). None of that is verified for these
 * modules — it is the most probable reading given every sibling behaves so.
 */
function oneTap(sign: 1 | -1, text: string) {
  return (
    doc: Parameters<EffectModule["effect"]>[0],
    host: Parameters<EffectModule["effect"]>[1],
  ) => {
    const frameSize = frameSizeFor(doc.bps);
    const { min, max } = host.getSampleValueLimits(doc.bps);

    return runSkeleton(doc, host, { text, canCancel: false }, (pass, progress) => {
      let xprev = getSample(pass.data, pass.start, doc.bps);
      for (let p = pass.start + frameSize; p < pass.end; p += frameSize) {
        const x = getSample(pass.data, p, doc.bps);
        let y = Math.floor((x + sign * xprev) / 2);
        if (y < min) y = min;
        else if (y > max) y = max;
        setSample(pass.data, p, y, doc.bps);
        xprev = x;
        if (progress.tick()) return kModNoError;
      }
    });
  };
}

/**
 * Two-tap recursive filters. `[DOC]`
 *
 * The `0.25·y(n−2)` is expressed here as `/4`, integer and truncating — the house
 * convention, and the only reading consistent with these modules shipping no FPU
 * build. Unverified.
 */
function twoTap(kind: "bandpass" | "bandstop", text: string) {
  return (
    doc: Parameters<EffectModule["effect"]>[0],
    host: Parameters<EffectModule["effect"]>[1],
  ) => {
    const frameSize = frameSizeFor(doc.bps);
    const { min, max } = host.getSampleValueLimits(doc.bps);

    return runSkeleton(doc, host, { text, canCancel: false }, (pass, progress) => {
      const first = getSample(pass.data, pass.start, doc.bps);
      let x2 = first;
      let x1 = first;
      let y2 = first;
      let y1 = first;

      for (let p = pass.start + frameSize; p < pass.end; p += frameSize) {
        const x = getSample(pass.data, p, doc.bps);
        let y =
          kind === "bandpass"
            ? x - x2 - Math.trunc(y2 / 4)
            : Math.floor((x + x2 + Math.trunc(y2 / 4)) / 2);
        if (y < min) y = min;
        else if (y > max) y = max;
        setSample(pass.data, p, y, doc.bps);
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        if (progress.tick()) return kModNoError;
      }
    });
  };
}

export const LowPassFIR: EffectModule = {
  name: "Low Pass FIR",
  signature: "LowPassFIR",
  group: "FIR/IIR Filters",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Filtering…",
  strings: [NO_OPTIONS],
  effect: oneTap(1, "Filtering…"),
};

export const HighPassFIR: EffectModule = {
  name: "High Pass FIR",
  signature: "HighPassFIR",
  group: "FIR/IIR Filters",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Filtering…",
  strings: [NO_OPTIONS],
  effect: oneTap(-1, "Filtering…"),
};

export const BandpassFIR: EffectModule = {
  name: "Bandpass FIR",
  signature: "BandpassFIR",
  group: "FIR/IIR Filters",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Filtering…",
  strings: [NO_OPTIONS],
  // Named "FIR" but has a recursive term. Zeros at z=±1, poles at ±0.5i.
  effect: twoTap("bandpass", "Filtering…"),
};

export const BandstopFIR: EffectModule = {
  name: "Bandstop FIR",
  signature: "BandstopFIR",
  group: "FIR/IIR Filters",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Filtering…",
  strings: [NO_OPTIONS],
  // Zeros ±i, poles ±0.5.
  effect: twoTap("bandstop", "Filtering…"),
};
