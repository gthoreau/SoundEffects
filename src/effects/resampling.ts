/**
 * The Resampling group — **Resample Rate**, **Downsample Bits**, **Upsample Bits**.
 * SPEC §8 #25/#7/#34.
 *
 * The theme: **these modules implement no conversion at all.** Each validates,
 * optionally pre-processes, and delegates to a host `GlueRec` service. The DSP
 * lives in the app binary (`runtime/resample.ts`), which is why recovering the
 * host resamplers closed the last `[ASM]` DSP question rather than any of these.
 *
 * All three are whole-document operations. A partial selection triggers a
 * confirmation alert ("Do you want me to proceed and downsample the whole
 * sound?") — the host asks; `confirm` stands in for the answer.
 */

import { ItemType, type SettingsSchema } from "../runtime/schema.js";
import { kModCancel } from "../runtime/results.js";
import { doubleToFixed, fixedToDouble, type Fixed } from "../units.js";
import { frameSizeFor, getSample, setSample } from "../samples.js";
import type { EffectModule } from "../runtime/module.js";
import type { SoundDocument } from "../document.js";
import type { Host } from "../runtime/host.js";

/**
 * Force a whole-document selection, asking first if only part is selected.
 * Shared by all three modules.
 */
function requireWholeDocument(doc: SoundDocument, confirm?: () => boolean): boolean {
  const whole =
    doc.selSt === doc.selEnd ||
    (doc.selSt === 0 &&
      doc.selEnd >= doc.maxChSize() &&
      doc.firstSelCh === 1 &&
      doc.lastSelCh === doc.numChans);
  if (!whole && confirm && !confirm()) return false;
  doc.selSt = 0;
  doc.selEnd = 0;
  doc.firstSelCh = 1;
  doc.lastSelCh = doc.numChans;
  return true;
}

export interface ResampleRateSettings {
  /** The target rate, Fixed 16.16. Defaults to 44.1 kHz before any prefs exist. */
  newRate: Fixed;
  confirm?: () => boolean;
}

/**
 * **Resample Rate** — §8 #25. `[BINARY: Resample-Rate.md + Host-Resamplers.md]`
 *
 * Real sample-rate conversion of the whole document — performed entirely by
 * `ModResampleSoundRate`. The interpolation method is **drop-sample /
 * zero-order-hold**, not linear; see `runtime/resample.ts`.
 *
 * "Settings default to 44.1 kHz (Fixed 44100.0) before any prefs are ever saved."
 */
export const ResampleRate: EffectModule<ResampleRateSettings> = {
  name: "Resample Rate",
  signature: "ResampleRate",
  group: "Resampling",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Resampling…",

  defaults: () => ({ newRate: doubleToFixed(44100) }),

  schema: (): SettingsSchema => [
    // The dialog self-references the previous rate: "Resample the sound
    // (previously at N Hz) at [__] Hz", plus a rate-presets popup.
    { type: ItemType.editFieldNumDecimal, key: "newRate", label: "Resample the sound at:" },
    { type: ItemType.ignore, label: "Hz" },
  ],

  effect(doc, host, prefs) {
    if (!requireWholeDocument(doc, prefs.confirm)) return kModCancel;
    return host.resampleSoundRate(doc, prefs.newRate);
  },
};

export interface BitsSettings {
  newBps: number;
  confirm?: () => boolean;
}

export interface DownsampleBitsSettings extends BitsSettings {
  /** "Expert features" checkbox. */
  expert: boolean;
  /** 1 = Dither [__] LSB, 2 = Noise shaping. 1-based, as radios are. */
  expertMode: number;
  /** LSB amplitude for the Dither radio. Warns if >= 1. */
  ditherLSB: number;
}

export const ExpertMode = { dither: 1, noiseShaping: 2 } as const;

/**
 * **Downsample Bits** — §8 #7. `[BINARY: disassembly/Downsample-Bits.md]`
 *
 * Sets bps lower. The truncation itself is `ModResampleSoundBits`; the module
 * owns validation and one optional pre-pass.
 *
 * With "Expert features" + noise shaping on, it injects
 * `sample += clamp(Random()·noiseScale/32767)` into **every sample of the whole
 * document** before the host truncates, where `noiseScale ≈ 2^(oldBits−newBits)`.
 * The dither amplitude auto-scales to the size of the discarded LSBs rather than
 * being a fixed constant — which is the whole idea, and why the host's dither-free
 * truncation is a reasonable division of labour.
 *
 * **Open sub-question** (`Downsample-Bits.md`): the exact radio binding. The
 * traced path has no user-value dependency, so it reads as "Noise shaping"; the
 * "Dither [__] LSB" path was never located and may be dead code — which would be
 * entirely in character for the app that shipped a non-functional Undo. We
 * implement the traced path for both radios and record the doubt here.
 */
export const DownsampleBits: EffectModule<DownsampleBitsSettings> = {
  name: "Downsample Bits",
  signature: "DownsampleBits",
  group: "Resampling",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Downsampling…",

  defaults: () => ({
    newBps: 8,
    expert: false,
    expertMode: ExpertMode.noiseShaping,
    ditherLSB: 0.5,
  }),

  schema: (): SettingsSchema => [
    { type: ItemType.editFieldNumerical, key: "newBps", label: "Downsample to:" },
    { type: ItemType.ignore, label: "bits" },
    { type: ItemType.checkBox, key: "expert", label: "Expert features" },
    { type: ItemType.radioButton, key: "expertMode", options: ["Dither LSB in amplitude", "Noise shaping"] },
    { type: ItemType.editFieldNumDecimal, key: "ditherLSB", label: "LSB in amplitude" },
  ],

  effect(doc, host, prefs) {
    if (prefs.newBps >= doc.bps) return kModCancel; // use Upsample Bits
    if (!requireWholeDocument(doc, prefs.confirm)) return kModCancel;

    if (prefs.expert) {
      noiseShapePrePass(doc, host, doc.bps - prefs.newBps);
    }
    return host.resampleSoundBits(doc, prefs.newBps);
  },
};

/**
 * The pre-pass: `sample += Random()·noiseScale/32767` over every sample of every
 * channel — the selection having already been forced to the whole document.
 *
 * `noiseScale ≈ 2^bitsRemoved`. The original routes an integer through a SANE
 * float round-trip to get there (four chained `_FP68K` calls whose exact
 * selectors weren't identified); the round-trip reads as a precision-rounding
 * step, not a transcendental, so the result is the power of two.
 */
function noiseShapePrePass(doc: SoundDocument, host: Host, bitsRemoved: number): void {
  const frameSize = frameSizeFor(doc.bps);
  const { min, max } = host.getSampleValueLimits(doc.bps);
  const noiseScale = Math.pow(2, bitsRemoved);

  for (const ch of doc.channels) {
    const buf = ch.data;
    const limit = buf.length - (buf.length % frameSize);
    for (let p = 0; p < limit; p += frameSize) {
      let s = getSample(buf, p, doc.bps);
      s += Math.trunc((host.random.next() * noiseScale) / 32767);
      if (s < min) s = min;
      else if (s > max) s = max;
      setSample(buf, p, s, doc.bps);
    }
  }
}

/**
 * **Upsample Bits** — §8 #34. `[BINARY: Host-Resamplers.md]`
 *
 * Raises bps, whole document only, and nags you to use Downsample for
 * reductions. The host shifts samples left and zero-pads the new low bytes —
 * no interpolation of resolution, because there is none to invent.
 */
export const UpsampleBits: EffectModule<BitsSettings> = {
  name: "Upsample Bits",
  signature: "UpsampleBits",
  group: "Resampling",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Upsampling…",

  defaults: () => ({ newBps: 16 }),

  schema: (): SettingsSchema => [
    { type: ItemType.editFieldNumerical, key: "newBps", label: "Upsample to:" },
    { type: ItemType.ignore, label: "bits" },
  ],

  effect(doc, host, prefs) {
    if (prefs.newBps <= doc.bps) return kModCancel; // the nag: use Downsample
    if (!requireWholeDocument(doc, prefs.confirm)) return kModCancel;
    return host.resampleSoundBits(doc, prefs.newBps);
  },
};

/** Re-exported for hosts that want to show "previously at N Hz". */
export function rateInHz(rate: Fixed): number {
  return fixedToDouble(rate);
}
