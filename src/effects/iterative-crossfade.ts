/**
 * **Iterative Crossfade** — SPEC §8 #17.
 * `[BINARY: disassembly/Sample-Hose-Iterative-Crossfade.md]`
 *
 * "To run this module you must first have two channels selected. This module then
 * creates a new channel, which contains iterations of a gradual crossfade between
 * the two selected samples."
 *
 *     r   = pos / len              sweeping 0 → 1
 *     out = clamp(trunc( A·(1−r) + B·r ))
 *
 * A plain linear crossfade in double precision, truncated then clamped. It shares
 * *nothing* with its siblings — no RNG, no transcendentals, no tween — and ships
 * **no FPU build**, because add/sub/mul/div is all it needs.
 *
 * **Channels that have run out contribute digital zero.** It zero-pads rather than
 * skipping, so a short sound fades into or out of silence. That is the opposite of
 * Select Peak's skip-the-short-channel rule — the two conventions genuinely differ
 * in this app, and conflating them would be easy.
 */

import { frameSizeFor, getSample, setSample, silentByte } from "../samples.js";
import { channelRange, expandEmptySelection } from "../selection.js";
import { kModCancel, kModNoError, type ModResult } from "../runtime/results.js";
import { ItemType, type SettingsSchema } from "../runtime/schema.js";
import { Unit } from "../units.js";
import type { EffectModule } from "../runtime/module.js";

export interface IterativeCrossfadeSettings {
  iterations: number;
  /** Bytes. Zero silence on short samples gives the quasi-granular behaviour. */
  silenceLength: number;
  units: number;
}

export const IterativeCrossfade: EffectModule<IterativeCrossfadeSettings> = {
  name: "Iterative Crossfade",
  signature: "IterativeCrossfade",
  group: "Mike's Modules",
  version: "1.0.2",
  author: "Michael Norris",
  progressText: "Crossfading…",
  about:
    "To run this module you must first have two channels selected. This module then creates a new channel, which contains iterations of a gradual crossfade between the two selected samples.",

  defaults: () => ({ iterations: 10, silenceLength: 0, units: Unit.milliseconds }),

  schema: (): SettingsSchema => [
    { type: ItemType.editFieldNumerical, key: "iterations", label: "Number of Iterations:" },
    { type: ItemType.editFieldUnitDependent, key: "silenceLength", label: "Silence Length:" },
    { type: ItemType.unitsPopup, key: "units" },
  ],

  effect(doc, host, prefs): ModResult {
    if (doc.lastSelCh - doc.firstSelCh + 1 < 2) return kModCancel;

    expandEmptySelection(doc);
    host.setupProgress("Crossfading…");

    const frameSize = frameSizeFor(doc.bps);
    const { min, max } = host.getSampleValueLimits(doc.bps);

    const aIdx = doc.firstSelCh - 1;
    const bIdx = doc.firstSelCh;
    const a = doc.channels[aIdx]!.data;
    const b = doc.channels[bIdx]!.data;
    const ra = channelRange(doc, aIdx);
    const rb = channelRange(doc, bIdx);

    // l = the selection length, in frames.
    const l = Math.max(
      Math.floor((ra.end - ra.start) / frameSize),
      Math.floor((rb.end - rb.start) / frameSize),
    );
    if (l < 1) {
      host.closeProgress();
      return kModCancel;
    }

    const n = Math.max(1, prefs.iterations);
    const s = Math.floor(prefs.silenceLength / frameSize); // silence, in frames

    // New length = n·(l+s) − s: silence sits *between* iterations, not after the
    // last one.
    const outFrames = n * (l + s) - s;
    const out = new Uint8Array(outFrames * frameSize);
    out.fill(silentByte(doc.bps));

    for (let iter = 0; iter < n; iter++) {
      const base = iter * (l + s);
      for (let i = 0; i < l; i++) {
        // `[INTERPRETED]`: r sweeps 0→1 across *each* iteration, per the
        // recovery's "r sweeps 0→1 across each fade" and the About text's
        // "iterations of a gradual crossfade". The alternative reading — one
        // crossfade spread across all n iterations — is defensible from
        // "renders n iterations crossfading from A to B" and would make each
        // iteration a different static mix. See SPEC §14.
        const r = i / l;

        const pa = ra.start + i * frameSize;
        const pb = rb.start + i * frameSize;
        // Zero-pad, don't skip.
        const A = pa + frameSize <= ra.end ? getSample(a, pa, doc.bps) : 0;
        const B = pb + frameSize <= rb.end ? getSample(b, pb, doc.bps) : 0;

        // Double precision, then truncate, then clamp — in that order.
        let v = Math.trunc(A * (1 - r) + B * r);
        if (v < min) v = min;
        else if (v > max) v = max;
        setSample(out, (base + i) * frameSize, v, doc.bps);
      }
    }

    // Renders into a NEW channel.
    host.addNewChannel(doc);
    doc.channels[doc.numChans - 1]!.data = out;

    host.closeProgress();
    return kModNoError;
  },
};
