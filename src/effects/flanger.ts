/**
 * **Flanger** — SPEC §8 #12. `[BINARY: disassembly/Flanger.md]`
 *
 * A modulated-delay comb: a delayed copy mixed with the original, the delay swept
 * over Period by an LFO between Delay±Width. The most expensive module in the app
 * and — despite that — still non-cancellable.
 *
 * Four recovered facts that shape this port:
 *
 *  1. **No interpolation.** The delay position is computed to full 80-bit
 *     SANE-extended precision and then `fintrz`'d to a single integer byte offset
 *     before the one-and-only delay-line read. Sub-sample precision is computed
 *     and thrown away. (Sample Hose is the app's *only* interpolating DSP.)
 *  2. **Feedback is a sweep-direction flip, not a tap.** The whole per-sample
 *     routine is compiled twice. Feedback off walks **backward** (every read
 *     touches pristine history — Delay's trick); on walks **forward** (the sweep
 *     eventually reads its own already-mixed output — Echo's trick). There is no
 *     feedback-gain constant; the recursion's gain is just the wet weight.
 *  3. **LFO**: Sinusoidal is a real `sin()`, not a table. Triangular and Sawtooth
 *     are linear accumulators — reflect vs. hard-reset.
 *  4. **Envelope follower is block-peak**, not RMS and not a one-pole smoother:
 *     `ModGetPeak` over 1024-sample windows, linearly interpolated between blocks.
 *
 * The final mix, in contrast to all that float, is plain truncating integer math —
 * the house convention holds even here.
 *
 * Fixed-length and in-place: no `doExtend`, no `SetHandleSize`, `selEnd` untouched.
 */

import { frameSizeFor, getSample, setSample } from "../samples.js";
import { runSkeleton } from "../runtime/skeleton.js";
import { kModNoError } from "../runtime/results.js";
import { ItemType, type SettingsSchema } from "../runtime/schema.js";
import { Unit } from "../units.js";
import type { EffectModule } from "../runtime/module.js";

export const Modulation = { sinusoidal: 1, triangular: 2, sawtooth: 3 } as const;
export const LouderMeans = { longer: 1, shorter: 2 } as const;

/**
 * The on-disk `Setp`/prefs record (36 bytes) — durations as **byte counts** plus
 * a remembered display-units code.
 *
 * The units codes are cosmetic: they exist only so the dialog reopens showing the
 * right popup, and play no part in `effect()`'s maths. That has a real
 * consequence — a factory preset's *effective* millisecond timing only matches its
 * name at 22254.5454 Hz. Apply "Plain Flange – Medium" to a 44.1 kHz document and
 * its 245 stored bytes are half the intended delay.
 */
export interface FlangerSettings {
  /** 0–100. 100 = wet only, 0 = dry only. */
  flangePercent: number;
  feedback: boolean;
  /** Bytes. */
  delay: number;
  delayUnits: number;
  width: number;
  widthUnits: number;
  period: number;
  periodUnits: number;
  modulation: number;
  envelopeFollower: boolean;
  /** Bytes. */
  depth: number;
  depthUnits: number;
  louderMeans: number;
}

/** The block size the envelope follower scans over. */
const ENVELOPE_BLOCK_SAMPLES = 1024;

/**
 * The 9 factory `Setp` presets, decoded from the resource fork.
 *
 * Two things worth noticing. The five "Plain Flange" variants and "Soft Flange"
 * are **byte-identical except for Period** — "Soft Flange" is not a distinct
 * algorithm, just a sixth period with a friendlier name. And **Feedback is off in
 * all nine**, including the most aggressive Distorto: none of the factory presets
 * exercises the feedback path at all.
 *
 * Byte counts are as stored; the ms figures are their meaning at 22254.5454 Hz.
 */
export const FLANGER_PRESETS: ReadonlyArray<{ name: string; values: FlangerSettings }> = [
  presetOf("Plain Flange - Fast", 50, 134, 67, 17804, false, 0),
  presetOf("Plain Flange - Medium", 50, 245, 45, 22255, false, 0),
  presetOf("Plain Flange - Slow", 50, 245, 45, 50073, false, 0),
  presetOf("Plain Flange - Slower", 50, 245, 45, 77891, false, 0),
  presetOf("Plain Flange - Slowest", 50, 245, 45, 105709, false, 0),
  presetOf("Soft Flange", 50, 245, 45, 44509, false, 0),
  presetOf("Distorto - High", 100, 111, 89, 6676, true, 445),
  presetOf("Distorto - Medium", 70, 110, 88, 11127, true, 88),
  presetOf("Distorto - Low", 70, 111, 66, 13353, true, 44),
];

function presetOf(
  name: string,
  flangePercent: number,
  delay: number,
  width: number,
  period: number,
  envelopeFollower: boolean,
  depth: number,
): { name: string; values: FlangerSettings } {
  return {
    name,
    values: {
      flangePercent,
      feedback: false, // off in every factory preset
      delay,
      delayUnits: Unit.milliseconds,
      width,
      widthUnits: Unit.milliseconds,
      period,
      periodUnits: Unit.milliseconds,
      modulation: Modulation.sinusoidal, // always, in all 9
      envelopeFollower,
      depth,
      depthUnits: Unit.milliseconds,
      louderMeans: LouderMeans.longer, // always, in all 9
    },
  };
}

/**
 * The LFO — returns the delay offset in bytes, before truncation.
 *
 * Exported because it is the testable heart of the module and each waveform has a
 * distinct, recovered shape.
 */
export class FlangerLFO {
  /** The running accumulator for triangular/sawtooth. */
  private acc: number;
  /** Step sign for triangular's reflection. */
  private dir = 1;

  constructor(
    private readonly modulation: number,
    private readonly delay: number,
    private readonly width: number,
    /** Period in SAMPLES. */
    private readonly periodSamples: number,
  ) {
    // `[INTERPRETED]`: the accumulators' initial values aren't in the recovery.
    // Sawtooth hard-resets to Delay−Width, so starting there is the only
    // self-consistent choice; triangular starts at the centreline rising, which
    // matches Sinusoidal's phase-0 value and makes the three waveforms agree at
    // n=0. See SPEC §14.
    this.acc = modulation === Modulation.sawtooth ? delay - width : delay;
  }

  /** `n` = samples processed so far in this channel. */
  next(n: number): number {
    switch (this.modulation) {
      case Modulation.sinusoidal:
        // Phase is driven by a plain sample counter — not wall-clock, not a
        // fractional-Hz phase accumulator.
        return this.delay + this.width * Math.sin((2 * Math.PI * n) / this.periodSamples);

      case Modulation.triangular: {
        const v = this.acc;
        const step = (4 * this.width) / this.periodSamples;
        this.acc += this.dir * step;
        // Reflect at the rails.
        if (this.acc > this.delay + this.width) {
          this.acc = this.delay + this.width;
          this.dir = -1;
        } else if (this.acc < this.delay - this.width) {
          this.acc = this.delay - this.width;
          this.dir = 1;
        }
        return v;
      }

      case Modulation.sawtooth: {
        const v = this.acc;
        this.acc += (2 * this.width) / this.periodSamples;
        // Hard reset, not reflected.
        if (this.acc > this.delay + this.width) this.acc = this.delay - this.width;
        return v;
      }

      default:
        return this.delay;
    }
  }
}

/** Peak of one block of a channel, as `ModGetPeak` would report it. */
function blockPeak(
  data: Uint8Array,
  from: number,
  to: number,
  bps: number,
): number {
  const frameSize = frameSizeFor(bps);
  let peak = 0;
  const hi = Math.min(to, data.length - (data.length % frameSize));
  for (let p = Math.max(0, from); p + frameSize <= hi; p += frameSize) {
    const v = Math.abs(getSample(data, p, bps));
    if (v > peak) peak = v;
  }
  return peak;
}

export const Flanger: EffectModule<FlangerSettings> = {
  name: "Flanger",
  signature: "Flanger",
  group: "Echoes & Reverbs",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Flanging…",
  strings: [
    "",
    "",
    "The Flanger effect can only be applied to an existing sound. Please open or record one before trying to apply the effect again.",
  ],

  // The 60-byte in-memory struct's hard-coded fallbacks. Delay/Width/Period/Depth
  // are left at whatever NewHandle returned in the original — "a full
  // extended-precision default wasn't worth hand-coding" — so we supply the
  // Plain Flange - Medium values rather than ship uninitialised garbage.
  defaults: () => ({
    flangePercent: 80,
    feedback: false,
    delay: 245,
    delayUnits: Unit.milliseconds,
    width: 45,
    widthUnits: Unit.milliseconds,
    period: 22255,
    periodUnits: Unit.milliseconds,
    modulation: Modulation.sinusoidal,
    envelopeFollower: false,
    depth: 0,
    depthUnits: Unit.milliseconds,
    louderMeans: LouderMeans.longer,
  }),

  schema: (): SettingsSchema => [
    { type: ItemType.editFieldNumerical, key: "flangePercent", label: "Flange percentage:", min: 0, max: 100 },
    { type: ItemType.editFieldUnitDependent, key: "delay", label: "Delay:" },
    { type: ItemType.editFieldUnitDependent, key: "width", label: "Width:" },
    { type: ItemType.editFieldUnitDependent, key: "period", label: "Period:" },
    { type: ItemType.unitsPopup, key: "delayUnits" },
    { type: ItemType.checkBox, key: "feedback", label: "Feedback" },
    { type: ItemType.radioButton, key: "modulation", options: ["Sinusoidal", "Triangular", "Sawtooth"] },
    { type: ItemType.checkBox, key: "envelopeFollower", label: "Envelope Follower" },
    { type: ItemType.editFieldUnitDependent, key: "depth", label: "Depth:" },
    { type: ItemType.radioButton, key: "louderMeans", options: ["Longer", "Shorter"] },
  ],

  /** `0x32` is read, clamped to [0,100], and `100 − value` cached as the dry weight. */
  validate: (p) => ({
    ...p,
    flangePercent: Math.max(0, Math.min(100, p.flangePercent)),
  }),

  effect(doc, host, prefs) {
    const frameSize = frameSizeFor(doc.bps);
    const { min, max } = host.getSampleValueLimits(doc.bps);
    const wet = Math.max(0, Math.min(100, prefs.flangePercent));
    const dryWeight = 100 - wet;
    const periodSamples = Math.max(1, prefs.period / frameSize);
    const blockBytes = ENVELOPE_BLOCK_SAMPLES * frameSize;

    return runSkeleton(doc, host, { text: "Flanging…", canCancel: false }, (pass, progress) => {
      // The first Delay+Width bytes are consumed as delay-line seed and never
      // become an output position — though their audio is still readable as
      // history for later positions.
      let start = pass.start + prefs.delay + prefs.width;
      start -= start % frameSize;
      if (start >= pass.end) return;

      const buf = pass.data;

      // The fixed reference peak: the last <=1024 samples of the selection,
      // scanned once before the loop. Note the backward sweep starts there.
      const refPeak = prefs.envelopeFollower
        ? blockPeak(buf, Math.max(start, pass.end - blockBytes), pass.end, doc.bps)
        : 0;

      const lfo = new FlangerLFO(prefs.modulation, prefs.delay, prefs.width, periodSamples);

      // Feedback OFF -> backward (reads only pristine history).
      // Feedback ON  -> forward  (eventually reads its own mixed output).
      const forward = prefs.feedback;
      const frames = Math.floor((pass.end - start) / frameSize);

      let chunkPeak = refPeak;
      let currentBlock = -1;

      for (let n = 0; n < frames; n++) {
        const cursor = forward
          ? start + n * frameSize
          : pass.end - frameSize - n * frameSize;

        // 1. Dry read, at the write cursor, before any delay offset.
        const dry = getSample(buf, cursor, doc.bps);

        // 2. LFO -> float delay offset, then truncate toward zero. This is the
        //    fintrz that throws away all the precision above.
        let d3 = Math.trunc(lfo.next(n));

        // 3. Envelope follower.
        if (prefs.envelopeFollower) {
          const block = Math.floor(n / ENVELOPE_BLOCK_SAMPLES);
          if (block !== currentBlock) {
            currentBlock = block;
            const from = forward
              ? start + block * blockBytes
              : pass.end - (block + 1) * blockBytes;
            chunkPeak = blockPeak(buf, from, from + blockBytes, doc.bps);
          }
          const chunkPos = n % ENVELOPE_BLOCK_SAMPLES;
          const interpolatedPeak =
            refPeak + ((chunkPeak - refPeak) * chunkPos) / ENVELOPE_BLOCK_SAMPLES;
          const adj = Math.trunc((interpolatedPeak * prefs.depth) / max);
          if (prefs.louderMeans === LouderMeans.longer) d3 += adj;
          else d3 -= adj;
        }

        // 4. The delay can never go negative — it must never read ahead of the
        //    write cursor.
        if (d3 < 0) d3 = 0;
        // `[INTERPRETED]`: frame-align the wet read. The recovery shows the
        // 8-bit path reading one byte at `cursor - d3` with no alignment step,
        // which is moot at frameSize 1; whether the wide path aligns is not
        // recorded. Reading unaligned would splice two frames into garbage, so
        // we align. See SPEC §14.
        d3 -= d3 % frameSize;

        // 5. Wet read.
        const src = cursor - d3;
        const wetSample = src >= 0 && src + frameSize <= buf.length
          ? getSample(buf, src, doc.bps)
          : 0;

        // 6. Mix — plain truncating integer math, despite everything above.
        let out = Math.trunc((dry * dryWeight) / 100) + Math.trunc((wetSample * wet) / 100);
        if (out < min) out = min;
        else if (out > max) out = max;

        // 7. Replace the dry sample in place; do not add to it.
        setSample(buf, cursor, out, doc.bps);

        if (progress.tick()) return kModNoError;
      }
    });
  },
};
