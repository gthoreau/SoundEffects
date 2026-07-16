/**
 * The three IIR filters — **High Pass IIR**, **Low Pass IIR**, **Shelving**.
 * SPEC §8 #15/#20/#30. `[BINARY: disassembly/Dither-IIR-Filters.md]`
 *
 *     High Pass  y(n) = (A·x(n))/10 − (B·y(n−1))/10
 *     Low Pass   y(n) = (A·x(n))/10 + (B·y(n−1))/10
 *     Shelving   y(n) = x(n) + (A·x(n−1))/10 + (B·y(n−1))/10
 *
 * The published equations are `y(n) = a·x(n) ± b·y(n−1)` and they hold — but the
 * *unwritten* details are where the sound lives, and there are three of them:
 *
 *   1. **Coefficients are quantised to one decimal place and truncated**:
 *      `A = trunc(a·10)`. Type 0.55, get 0.5.
 *   2. **Each term is truncated independently** by its own integer divide — and
 *      in a recursive filter that error feeds back forever.
 *   3. **`y(n−1)` is seeded with the first sample, not zero**, and that first
 *      sample is left unmodified.
 *
 * There is **no coefficient range checking anywhere**. Enter b = 2.0 and the
 * filter blows up sample by sample, clamping into a square wave. That is the
 * intended experience, and validating it away would be a bug.
 */

import { frameSizeFor, getSample, setSample } from "../samples.js";
import { runSkeleton } from "../runtime/skeleton.js";
import { kModNoError } from "../runtime/results.js";
import { ItemType, type SettingsSchema } from "../runtime/schema.js";
import type { EffectModule } from "../runtime/module.js";

export interface IIRSettings {
  /** Coefficient a, as typed. Quantised to 0.1 at run time. */
  a: number;
  /** Coefficient b, as typed. */
  b: number;
}

type IIRKind = "highpass" | "lowpass" | "shelving";

const iirSchema = (): SettingsSchema => [
  { type: ItemType.ignore, label: "Coefficients" },
  { type: ItemType.editFieldNumDecimal, key: "a", label: "a:" },
  { type: ItemType.editFieldNumDecimal, key: "b", label: "b:" },
];

/**
 * The shared kernel. The three modules differ only in the inner expression —
 * `add.l` vs `sub.l` vs two adds, one instruction apart in the original.
 */
function iirEffect(kind: IIRKind, text: string) {
  return (
    doc: Parameters<EffectModule<IIRSettings>["effect"]>[0],
    host: Parameters<EffectModule<IIRSettings>["effect"]>[1],
    prefs: IIRSettings,
  ) => {
    const frameSize = frameSizeFor(doc.bps);
    const { min, max } = host.getSampleValueLimits(doc.bps);

    // Quantise to one decimal and truncate — this is what settings() stored.
    const A = Math.trunc(prefs.a * 10);
    const B = Math.trunc(prefs.b * 10);

    return runSkeleton(doc, host, { text, canCancel: false }, (pass, progress) => {
      const first = getSample(pass.data, pass.start, doc.bps);
      // Seeded with the signal's own first value, not silence: no start-up
      // transient, but the first sample passes through unmodified.
      let yprev = first;
      let xprev = first;

      for (let p = pass.start + frameSize; p < pass.end; p += frameSize) {
        const x = getSample(pass.data, p, doc.bps);

        let y: number;
        switch (kind) {
          case "lowpass":
            y = Math.trunc((A * x) / 10) + Math.trunc((B * yprev) / 10);
            break;
          case "highpass":
            y = Math.trunc((A * x) / 10) - Math.trunc((B * yprev) / 10);
            break;
          case "shelving":
            // The feed-forward tap uses the ORIGINAL input, saved before the
            // write — pristine input, not filtered output.
            y = x + Math.trunc((A * xprev) / 10) + Math.trunc((B * yprev) / 10);
            break;
        }

        if (y < min) y = min;
        else if (y > max) y = max;

        setSample(pass.data, p, y, doc.bps);
        yprev = y;
        xprev = x;
        if (progress.tick()) return kModNoError;
      }
    });
  };
}

export const HighPassIIR: EffectModule<IIRSettings> = {
  name: "High Pass IIR",
  signature: "HighPassIIR",
  group: "FIR/IIR Filters",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Filtering…",
  defaults: () => ({ a: 1, b: 0.5 }),
  schema: iirSchema,
  effect: iirEffect("highpass", "Filtering…"),
};

export const LowPassIIR: EffectModule<IIRSettings> = {
  name: "Low Pass IIR",
  signature: "LowPassIIR",
  group: "FIR/IIR Filters",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Filtering…",
  defaults: () => ({ a: 0.5, b: 0.5 }),
  schema: iirSchema,
  effect: iirEffect("lowpass", "Filtering…"),
};

export const Shelving: EffectModule<IIRSettings> = {
  name: "Shelving",
  signature: "Shelving",
  group: "FIR/IIR Filters",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Filtering…",
  // Its help text ends mid-sentence in TEXT 1000. Ship the truncated sentence.
  about: "The response is that of a shelving filter, with.",
  defaults: () => ({ a: 0.5, b: 0.5 }),
  schema: iirSchema,
  effect: iirEffect("shelving", "Filtering…"),
};
