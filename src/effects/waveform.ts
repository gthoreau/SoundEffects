/**
 * **Waveform Generator** — SPEC §8 #35. `[RES]`, not byte-verified.
 *
 * Synthesises into the selection: Length, Frequency (Hz), Amplitude (%), and a
 * Sine / Square / Sawtooth radio.
 *
 * `[INTERPRETED]`: the DITL gives the fields; nothing gives the waveform
 * conventions. Sawtooth's direction and phase, whether the amplitude percentage
 * is of the bit depth's max or of 2^(bps−1) (Dither uses the latter, and is off
 * by one for it), and whether Length extends the selection or is clamped to it —
 * all inference. See SPEC §14.
 */

import { frameSizeFor, setSample } from "../samples.js";
import { runSkeleton } from "../runtime/skeleton.js";
import { kModNoError } from "../runtime/results.js";
import { ItemType, type SettingsSchema } from "../runtime/schema.js";
import { fixedToDouble, Unit } from "../units.js";
import type { EffectModule } from "../runtime/module.js";

export const WaveType = { sine: 1, square: 2, sawtooth: 3 } as const;

export interface WaveformSettings {
  /** Bytes. 0 = the whole selection. */
  length: number;
  units: number;
  /** Hz. */
  frequency: number;
  /** Percent of full scale. */
  amplitude: number;
  /** 1 = Sine, 2 = Square, 3 = Sawtooth. */
  waveType: number;
}

/** One cycle of the chosen wave at phase `p` (0..1), in -1..1. */
export function waveSample(waveType: number, p: number): number {
  switch (waveType) {
    case WaveType.sine:
      return Math.sin(2 * Math.PI * p);
    case WaveType.square:
      // Zero counts as positive, matching Weird Tempo's sign convention.
      return p < 0.5 ? 1 : -1;
    case WaveType.sawtooth:
      // Rising ramp -1 -> 1, reset. Direction is inference.
      return 2 * p - 1;
    default:
      return 0;
  }
}

export const WaveformGenerator: EffectModule<WaveformSettings> = {
  name: "Waveform Generator",
  signature: "WaveformGenerator",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Generating…",

  defaults: () => ({
    length: 0,
    units: Unit.milliseconds,
    frequency: 440,
    amplitude: 100,
    waveType: WaveType.sine,
  }),

  schema: (): SettingsSchema => [
    { type: ItemType.editFieldUnitDependent, key: "length", label: "Length:" },
    { type: ItemType.unitsPopup, key: "units" },
    { type: ItemType.editFieldNumDecimal, key: "frequency", label: "Frequency:" },
    { type: ItemType.ignore, label: "Hz" },
    { type: ItemType.editFieldNumerical, key: "amplitude", label: "Amplitude:" },
    { type: ItemType.ignore, label: "%" },
    { type: ItemType.radioButton, key: "waveType", options: ["Sine", "Square", "Sawtooth"] },
  ],

  effect(doc, host, prefs) {
    const frameSize = frameSizeFor(doc.bps);
    const { min, max } = host.getSampleValueLimits(doc.bps);
    const rate = fixedToDouble(doc.rate);
    // Amplitude as a fraction of the depth's max. Dither scales by 2^(bps-1)
    // instead and overshoots by one; which convention this module uses is not
    // recorded, so we take the safe one that cannot clip on its own.
    const amp = (max * prefs.amplitude) / 100;

    return runSkeleton(doc, host, { text: "Generating…", canCancel: false }, (pass, progress) => {
      const span = prefs.length > 0 ? Math.min(prefs.length, pass.end - pass.start) : pass.end - pass.start;
      const frames = Math.floor(span / frameSize);
      if (frames < 1 || rate <= 0) return;

      const cyclesPerSample = prefs.frequency / rate;
      for (let i = 0; i < frames; i++) {
        const phase = (i * cyclesPerSample) % 1;
        // Truncating, as the house style demands even where the maths is float.
        let v = Math.trunc(amp * waveSample(prefs.waveType, phase));
        if (v < min) v = min;
        else if (v > max) v = max;
        setSample(pass.data, pass.start + i * frameSize, v, doc.bps);
        if (progress.tick()) return kModNoError;
      }
    });
  },
};
