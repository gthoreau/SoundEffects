/**
 * **Echo** and **Delay** — SPEC §8 #9/#4. `[BINARY: disassembly/Echo-Delay.md]`
 *
 * These are the *same* offset-and-scale kernel, and the sweep direction over the
 * in-place buffer is the whole trick:
 *
 *     Echo  (forward):  dst[i+D] = clamp( dst[i+D] + trunc(src[i]·strength/100) )
 *     Delay (backward): dst[i]   = clamp( dst[i]   + trunc(src[i−D]·strength/100) )
 *
 * Both are literally `y[n] = clamp(y[n] + trunc(y[n−D]·strength/100))` — the same
 * single-tap recurrence Robotize uses, with D and strength as parameters.
 *
 *   - **Echo sweeps forward**, so a write at `i+D` is read again as a source once
 *     the cursor reaches it. One pass is genuinely recursive and produces the
 *     entire exponentially-decaying echo train by itself.
 *   - **Delay sweeps backward**, so every read at `i−D` happens before that
 *     position is ever a write target. Every read is pristine: exactly one copy,
 *     no repeats.
 *
 * The decay curve you hear is an *emergent property of the recursion*, not of any
 * decay maths — Echo's `echoCount` loop below only sizes the buffer.
 */

import { frameSizeFor, getSample, setSample } from "../samples.js";
import { ItemType, type SettingsSchema } from "../runtime/schema.js";
import { growAndSilence, resolveDoExtend, runSkeleton } from "../runtime/skeleton.js";
import { kModNoError } from "../runtime/results.js";
import { Unit } from "../units.js";
import type { EffectModule } from "../runtime/module.js";

/** `{ long delay; short strength; Boolean onlyOn; short onlyCount; }` — 10 bytes. */
export interface EchoSettings {
  /** In bytes, always (a unit-dependent field). */
  delay: number;
  strength: number;
  onlyOn: boolean;
  onlyCount: number;
  units: number;
}

/** `{ long delay; short strength; }` — 6 bytes. No checkbox, no count. */
export interface DelaySettings {
  delay: number;
  strength: number;
  units: number;
}

/** Recovered constants: the decay sizing loop stops at ≤8% or 80 echoes. */
const DECAY_AMP_FLOOR = 8;
const MAX_ECHOES = 80;

/**
 * Echo's "until it decays" range, simulated *before* touching any samples —
 * purely to decide how far the buffer must grow. This loop never reads or writes
 * audio.
 *
 * At strength 70% that's ⌈log(8/100)/log(0.7)⌉ = 8 echoes; the 80-echo cap only
 * bites at strengths close to 100%.
 */
function echoMixLimit(end: number, delayBytes: number, strength: number): number {
  let mixLimit = end;
  let amp = 100;
  let echoCount = 1;
  do {
    mixLimit += delayBytes;
    amp = Math.trunc((amp * strength) / 100); // integer, truncating
    echoCount++;
  } while (amp > DECAY_AMP_FLOOR && echoCount < MAX_ECHOES);
  return mixLimit;
}

const durationSchema = (extra: SettingsSchema = []): SettingsSchema => [
  { type: ItemType.editFieldUnitDependent, key: "delay", label: "Delay:" },
  { type: ItemType.unitsPopup, key: "units" },
  { type: ItemType.editFieldNumerical, key: "strength", label: "Strength:" },
  { type: ItemType.ignore, label: "%" },
  ...extra,
];

export const Echo: EffectModule<EchoSettings> = {
  name: "Echo",
  signature: "Echo",
  group: "Echoes & Reverbs",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Echoing…",

  defaults: () => ({
    delay: 3500,
    strength: 70,
    onlyOn: false,
    onlyCount: 3,
    units: Unit.milliseconds,
  }),

  schema: () =>
    durationSchema([
      { type: ItemType.checkBox, key: "onlyOn", label: "Only" },
      { type: ItemType.editFieldNumerical, key: "onlyCount", label: "echoes" },
    ]),

  effect(doc, host, prefs) {
    const frameSize = frameSizeFor(doc.bps);
    const { min, max } = host.getSampleValueLimits(doc.bps);
    // Delay is frame-aligned before use.
    const delayBytes = prefs.delay - (prefs.delay % frameSize);
    const doExtend = resolveDoExtend(host);

    let maxSelEnd = 0;

    const result = runSkeleton(doc, host, { text: "Echoing…", canCancel: false }, (pass, progress) => {
      let mixLimit = prefs.onlyOn
        ? // "Only N echoes": a hard cap, no decay maths at all.
          pass.end + prefs.onlyCount * delayBytes
        : echoMixLimit(pass.end, delayBytes, prefs.strength);

      // If extension is off, the decay is confined to the selection's own overlap.
      if (!doExtend) mixLimit = pass.end;

      if (mixLimit > pass.data.length) {
        // Grows, silences the new tail, and degrades gracefully: if the grow only
        // partially succeeds the range silently shrinks, with no error surfaced.
        mixLimit = Math.min(mixLimit, growAndSilence(doc, pass.index, mixLimit));
      }
      const buf = doc.channels[pass.index]!.data;

      if (mixLimit <= pass.start + delayBytes) return; // degenerate: no-op
      if (mixLimit > maxSelEnd) maxSelEnd = mixLimit;

      // Forward: the destination of an early sample becomes a later source read.
      for (let src = pass.start; src < mixLimit - delayBytes; src += frameSize) {
        const here = getSample(buf, src, doc.bps);
        const dst = src + delayBytes;
        let ahead = getSample(buf, dst, doc.bps);
        ahead += Math.trunc((here * prefs.strength) / 100);
        if (ahead < min) ahead = min;
        else if (ahead > max) ahead = max;
        setSample(buf, dst, ahead, doc.bps);
        if (progress.tick()) return kModNoError;
      }
    });

    // selEnd extends to the max mixLimit across processed channels — channels
    // drift independently, same philosophy as Weird Tempo.
    if (maxSelEnd > doc.selEnd) doc.selEnd = maxSelEnd;
    return result;
  },
};

export const Delay: EffectModule<DelaySettings> = {
  name: "Delay",
  signature: "Delay",
  group: "Echoes & Reverbs",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Delaying…",

  defaults: () => ({ delay: 3500, strength: 70, units: Unit.milliseconds }),
  schema: () => durationSchema(),

  effect(doc, host, prefs) {
    const frameSize = frameSizeFor(doc.bps);
    const { min, max } = host.getSampleValueLimits(doc.bps);
    const delayBytes = prefs.delay - (prefs.delay % frameSize);
    const doExtend = resolveDoExtend(host);

    let maxSelEnd = 0;

    const result = runSkeleton(doc, host, { text: "Delaying…", canCancel: false }, (pass, progress) => {
      // Always exactly one delay-length, never a decay loop.
      let mixLimit = doExtend ? pass.end + delayBytes : pass.end;

      if (mixLimit > pass.data.length) {
        mixLimit = Math.min(mixLimit, growAndSilence(doc, pass.index, mixLimit));
      }
      const buf = doc.channels[pass.index]!.data;

      if (mixLimit <= pass.start + delayBytes) return;
      if (mixLimit > maxSelEnd) maxSelEnd = mixLimit;

      // Backward: every read at dst-delayBytes precedes that position ever being
      // written, so it is always the untouched original. One clean copy.
      const firstDst = pass.start + delayBytes;
      let dst = mixLimit - frameSize;
      dst -= dst % frameSize;
      for (; dst >= firstDst; dst -= frameSize) {
        const src = dst - delayBytes;
        const here = getSample(buf, src, doc.bps);
        let ahead = getSample(buf, dst, doc.bps);
        ahead += Math.trunc((here * prefs.strength) / 100);
        if (ahead < min) ahead = min;
        else if (ahead > max) ahead = max;
        setSample(buf, dst, ahead, doc.bps);
        if (progress.tick()) return kModNoError;
      }
    });

    if (maxSelEnd > doc.selEnd) doc.selEnd = maxSelEnd;
    return result;
  },
};
