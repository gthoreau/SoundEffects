/**
 * The published one-liners — SPEC §8 #8/#10/#11/#16/#22/#26/#31.
 * **All `[DOC]`, none byte-verified.**
 *
 * §15's own warning applies: "Smooth proved docs can lie, so treat as
 * high-confidence-but-unproven — though nothing here is complex enough to hide a
 * surprise on Smooth's scale." Agreed for Invert and Reverse. Less obviously so
 * for Normalize and the fades, where the *rounding* is unspecified and the
 * house style (truncate, floor, integer-only) is inferred rather than read.
 */

import { frameSizeFor, getSample, setSample, silentByte } from "../samples.js";
import { perSampleEffect, runSkeleton } from "../runtime/skeleton.js";
import { kModCancel, kModNoError, type ModResult } from "../runtime/results.js";
import { ItemType, type SettingsSchema } from "../runtime/schema.js";
import { Unit } from "../units.js";
import type { EffectModule } from "../runtime/module.js";

const NO_OPTIONS = "Sorry, man. I haven't got any options.";

/** **Invert** — negate the waveform. §8 #16. `[DOC]` */
export const Invert: EffectModule = {
  name: "Invert",
  signature: "Invert",
  group: "Misc",
  version: "1.0",
  author: "Alberto Ricci",
  progressText: "Inverting…",
  strings: [NO_OPTIONS],
  effect(doc, host) {
    // Negating min (-128 at 8-bit) gives 128, which the clamp pins to 127 —
    // the asymmetry of two's complement, preserved rather than special-cased.
    return perSampleEffect(doc, host, { text: "Inverting…", canCancel: false }, (s) => -s);
  },
};

/**
 * **Reverse** — §8 #26. `[DOC]`
 *
 * "You'll finally be able to hear those weird satanic messages hidden in your
 * sounds!" The low-memory path asks to proceed with a possibly-incorrect
 * reversal — a reverse that may garble under pressure is worth keeping, but it
 * needs a memory model to hang off, so it lands with the UI shell.
 */
export const Reverse: EffectModule = {
  name: "Reverse",
  signature: "Reverse",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Reversing…",
  strings: [NO_OPTIONS],
  effect(doc, host) {
    const frameSize = frameSizeFor(doc.bps);
    return runSkeleton(doc, host, { text: "Reversing…", canCancel: false }, (pass) => {
      const frames = Math.floor((pass.end - pass.start) / frameSize);
      for (let i = 0; i < Math.floor(frames / 2); i++) {
        const a = pass.start + i * frameSize;
        const b = pass.start + (frames - 1 - i) * frameSize;
        for (let k = 0; k < frameSize; k++) {
          const t = pass.data[a + k]!;
          pass.data[a + k] = pass.data[b + k]!;
          pass.data[b + k] = t;
        }
      }
    });
  },
};

/**
 * **Silence** — §8 #31. `[DOC+RES]`
 *
 * Replaces the selection with silence; with an insertion point instead offers
 * "Insert [__ units] of silence." Insertion is an editing operation (`edit.ts`
 * has `insertSilence`), so this module covers the replace case.
 */
export const Silence: EffectModule = {
  name: "Silence",
  signature: "Silence",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Silencing…",
  effect(doc, host) {
    const silent = silentByte(doc.bps);
    return runSkeleton(doc, host, { text: "Silencing…", canCancel: false }, (pass) => {
      pass.data.fill(silent, pass.start, pass.end);
    });
  },
};

/**
 * **Draw Line** — §8 #8. `[DOC]`
 *
 * Replaces the selection with a straight line from the first sample to the last.
 * A de-click tool and an instant ramp generator.
 */
export const DrawLine: EffectModule = {
  name: "Draw Line",
  signature: "DrawLine",
  group: "Misc",
  version: "1.0",
  author: "Alberto Ricci",
  progressText: "Drawing…",
  strings: [NO_OPTIONS],
  effect(doc, host) {
    const frameSize = frameSizeFor(doc.bps);
    return runSkeleton(doc, host, { text: "Drawing…", canCancel: false }, (pass, progress) => {
      const frames = Math.floor((pass.end - pass.start) / frameSize);
      if (frames < 2) return;
      const first = getSample(pass.data, pass.start, doc.bps);
      const last = getSample(pass.data, pass.start + (frames - 1) * frameSize, doc.bps);
      for (let i = 0; i < frames; i++) {
        const v = Math.trunc(first + ((last - first) * i) / (frames - 1));
        setSample(pass.data, pass.start + i * frameSize, v, doc.bps);
        if (progress.tick()) return kModNoError;
      }
    });
  },
};

export interface FadeSettings {
  /** In bytes, always. */
  time: number;
  units: number;
}

const fadeSchema = (): SettingsSchema => [
  { type: ItemType.editFieldUnitDependent, key: "time", label: "Fading time:" },
  { type: ItemType.unitsPopup, key: "units" },
];

/**
 * **Fade In** / **Fade Out** — §8 #10/#11. `[DOC]`
 *
 * Linear fades over `time`. These shipped FPU and PowerPC variants — float-capable
 * builds of the same algorithm — which tells us the gain ramp was float on those
 * paths. We take the integer reading (`s·i/n`, truncating) because that is what
 * the 68k build must have done and what §2.2 calls the house discipline; a
 * disassembly could refine it. See SPEC §14.
 */
export const FadeIn: EffectModule<FadeSettings> = {
  name: "Fade In",
  signature: "FadeIn",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Fading in…",
  defaults: () => ({ time: 0, units: Unit.milliseconds }),
  schema: fadeSchema,
  effect(doc, host, prefs) {
    return fadeEffect(doc, host, prefs, "in");
  },
};

export const FadeOut: EffectModule<FadeSettings> = {
  name: "Fade Out",
  signature: "FadeOut",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Fading out…",
  defaults: () => ({ time: 0, units: Unit.milliseconds }),
  schema: fadeSchema,
  effect(doc, host, prefs) {
    return fadeEffect(doc, host, prefs, "out");
  },
};

function fadeEffect(
  doc: Parameters<EffectModule<FadeSettings>["effect"]>[0],
  host: Parameters<EffectModule<FadeSettings>["effect"]>[1],
  prefs: FadeSettings,
  dir: "in" | "out",
): ModResult {
  const frameSize = frameSizeFor(doc.bps);
  const text = dir === "in" ? "Fading in…" : "Fading out…";

  return runSkeleton(doc, host, { text, canCancel: false }, (pass, progress) => {
    // A fade time of 0 means the whole selection — otherwise the field would have
    // to be filled in before the module did anything at all.
    const span = prefs.time > 0 ? Math.min(prefs.time, pass.end - pass.start) : pass.end - pass.start;
    const frames = Math.floor(span / frameSize);
    if (frames < 1) return;

    for (let i = 0; i < frames; i++) {
      const p = dir === "in" ? pass.start + i * frameSize : pass.end - (i + 1) * frameSize;
      const s = getSample(pass.data, p, doc.bps);
      setSample(pass.data, p, Math.trunc((s * i) / frames), doc.bps);
      if (progress.tick()) return kModNoError;
    }
  });
}

/**
 * **Normalize** — §8 #22. `[DOC+RES]`
 *
 * Amplify the selection to maximum without clipping.
 *
 * The multi-channel alert ("You are about to normalize only a part of this
 * sound's channels…" — Norm All / Just These / Cancel, where Norm All extends
 * the selection to all channels, *not in time*) is a host dialog; `scope` stands
 * in for the answer.
 *
 * Note the gain is computed **once across all selected channels**, not per
 * channel — otherwise normalising a stereo pair would destroy its balance.
 * That's the only reading consistent with "Norm All" being a meaningful choice,
 * but it is inference, not recovery.
 */
export interface NormalizeSettings {
  /** 1 = just the selected channels, 2 = all channels ("Norm All"). */
  scope: number;
}

export const NormalizeScope = { justThese: 1, all: 2 } as const;

export const Normalize: EffectModule<NormalizeSettings> = {
  name: "Normalize",
  signature: "Normalize",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Normalizing…",
  defaults: () => ({ scope: NormalizeScope.justThese }),

  effect(doc, host, prefs) {
    if (prefs.scope === NormalizeScope.all) {
      doc.firstSelCh = 1;
      doc.lastSelCh = doc.numChans;
    }
    const { max } = host.getSampleValueLimits(doc.bps);
    const { peak } = host.getPeak(doc);
    if (peak === 0) return kModCancel; // digital silence: nothing to scale

    // Integer percent, as Amplify takes — the house unit for gain.
    const gain = Math.trunc((max * 100) / peak);
    if (gain <= 100) return kModCancel; // already at or over full scale

    return perSampleEffect(doc, host, { text: "Normalizing…", canCancel: false }, (s) =>
      Math.trunc((s * gain) / 100),
    );
  },
};
