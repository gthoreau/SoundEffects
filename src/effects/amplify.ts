/**
 * **Amplify** — `s' = clamp(s × gain / 100)`. SPEC §8 #1, §7.8.
 *
 * `[SOURCE]` — the SDK's `Gain.c` *is* this module, so this is the one effect we
 * can port from real C rather than from a disassembly or a doc. It is the
 * reference: if the skeleton is shaped right, this file is nearly all metadata.
 */

import { ItemType, type SettingsSchema } from "../runtime/schema.js";
import { perSampleEffect } from "../runtime/skeleton.js";
import type { EffectModule } from "../runtime/module.js";

/** `struct ModSettingsRec { short gain; }` */
export interface AmplifySettings {
  gain: number;
}

export const Amplify: EffectModule<AmplifySettings> = {
  name: "Amplify",
  signature: "SampleEffect",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Amplifying by ",

  defaults: () => ({ gain: 200 }),

  schema: (): SettingsSchema => [
    { type: ItemType.editFieldNumerical, key: "gain", label: "Amplify by:", max: 32767 },
    { type: ItemType.ignore, label: "%" },
  ],

  /** Gain.c's `settings` clamps to a short's range. */
  validate: (p) => ({ gain: Math.min(p.gain, 32767) }),

  effect(doc, host, prefs) {
    // `mySample = mySample * myGain / 100` — integer, truncating. The rounding
    // scar is the point (§2.2), so this must not become a rounded multiply.
    return perSampleEffect(
      doc,
      host,
      { text: "Amplifying by ", canCancel: false },
      (s) => Math.trunc((s * prefs.gain) / 100),
    );
  },
};
