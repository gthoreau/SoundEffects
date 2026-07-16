/**
 * **Dither** — SPEC §8 #6. `[BINARY: disassembly/Dither-IIR-Filters.md]`
 *
 *     out = clamp( x + (amplitude · Random()) / 32767 )
 *
 * Pure integer, no floating point. Uses the **Toolbox `_Random` trap** — the same
 * RNG as Noise, *not* the ANSI-C LCG the Norris modules use — but it **adds**
 * where Noise replaces.
 *
 * Its dialog is module-drawn (DITL 128), not the host schema: explicit OK/Cancel,
 * the label "Add", two radios and two fields, then "of random noise". The schema
 * below therefore describes the settings for a host that wants to generate a
 * form, but it is a reconstruction of a hand-drawn dialog, not a `[SOURCE]` one.
 */

import { ItemType, type SettingsSchema } from "../runtime/schema.js";
import { perSampleEffect } from "../runtime/skeleton.js";
import type { EffectModule } from "../runtime/module.js";

export interface DitherSettings {
  /** 1 = percent of max amplitude, 2 = absolute points. 1-based, as radios are. */
  mode: number;
  /** Percent of max amplitude. */
  percent: number;
  /** Absolute points. */
  points: number;
}

export const DitherMode = { percent: 1, points: 2 } as const;

/**
 * `amplitude = (pct · 2^(bps−1)) / 100` — integer, truncating.
 *
 * This reproduces the About text's own examples exactly: 10% gives **12** points
 * at 8-bit and **3276** at 16-bit. Note `2^(bps−1)` is 128/32768 — one *more*
 * than the true max sample (127/32767). The overshoot is real and the per-sample
 * clamp absorbs it; don't "fix" it to 2^(bps−1)−1 or the documented examples stop
 * matching.
 */
export function ditherAmplitude(settings: DitherSettings, bps: number): number {
  if (settings.mode === DitherMode.points) return settings.points;
  return Math.trunc((settings.percent * Math.pow(2, bps - 1)) / 100);
}

export const Dither: EffectModule<DitherSettings> = {
  name: "Dither",
  signature: "Dither",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Dithering…",

  defaults: () => ({ mode: DitherMode.percent, percent: 10, points: 12 }),

  schema: (): SettingsSchema => [
    { type: ItemType.ignore, label: "Add" },
    { type: ItemType.radioButton, key: "mode", options: ["%", "points"] },
    { type: ItemType.editFieldNumerical, key: "percent", label: "%" },
    { type: ItemType.editFieldNumerical, key: "points", label: "points" },
    { type: ItemType.ignore, label: "of random noise" },
  ],

  effect(doc, host, prefs) {
    const amp = ditherAmplitude(prefs, doc.bps);
    return perSampleEffect(doc, host, { text: "Dithering…", canCancel: false }, (s) => {
      const r = host.random.next(); // signed word
      return s + Math.trunc((amp * r) / 32767);
    });
  },
};
