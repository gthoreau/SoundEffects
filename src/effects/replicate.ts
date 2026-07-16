/**
 * **Replicate** — SPEC §8 #24. `[DOC, rich]`, not byte-verified.
 *
 * Clones the selection N times with optional gaps. Its built-in help is a
 * manifesto for tape-loop rhythm building, and §8 says to quote it in the UI.
 *
 * Two behaviours the docs *do* pin down and that are worth keeping:
 *   - **Divide-by-zero is a friendly alert, not an error** — it falls back to
 *     gap = selection length and carries on.
 *   - **Out of memory offers "replicate as many times as possible"** rather than
 *     refusing. Partial success is a first-class outcome in this app.
 */

import { frameSizeFor, silentByte } from "../samples.js";
import { channelRange, expandEmptySelection } from "../selection.js";
import { resolveDoExtend } from "../runtime/skeleton.js";
import { kModCancel, kModNoError, type ModResult } from "../runtime/results.js";
import { ItemType, type SettingsSchema } from "../runtime/schema.js";
import { Unit } from "../units.js";
import type { EffectModule } from "../runtime/module.js";

export const GapMode = {
  none: 1,
  selectionDividedBy: 2,
  selectionMultipliedBy: 3,
  fixed: 4,
} as const;

export interface ReplicateSettings {
  /** Number of copies. */
  copies: number;
  /** 1 = No Gaps, 2 = divided by, 3 = multiplied by, 4 = fixed units. */
  gapMode: number;
  gapDivisor: number;
  gapMultiplier: number;
  /** Bytes, for gapMode 4. */
  gapLength: number;
  units: number;
}

/**
 * The gap between copies, in bytes.
 *
 * Exported because the divide-by-zero fallback is a documented behaviour rather
 * than an implementation detail: "Divide-by-zero → friendly alert, uses gap =
 * selection length."
 */
export function gapBytes(prefs: ReplicateSettings, selectionBytes: number): number {
  switch (prefs.gapMode) {
    case GapMode.none:
      return 0;
    case GapMode.selectionDividedBy:
      // The alert is the host's; the fallback is ours.
      if (prefs.gapDivisor === 0) return selectionBytes;
      return Math.trunc(selectionBytes / prefs.gapDivisor);
    case GapMode.selectionMultipliedBy:
      return Math.trunc(selectionBytes * prefs.gapMultiplier);
    case GapMode.fixed:
      return prefs.gapLength;
    default:
      return 0;
  }
}

export const Replicate: EffectModule<ReplicateSettings> = {
  name: "Replicate",
  signature: "Replicate",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Replicating…",

  defaults: () => ({
    copies: 2,
    gapMode: GapMode.none,
    gapDivisor: 2,
    gapMultiplier: 2,
    gapLength: 0,
    units: Unit.milliseconds,
  }),

  schema: (): SettingsSchema => [
    { type: ItemType.editFieldNumerical, key: "copies", label: "Number of copies:" },
    {
      type: ItemType.radioButton,
      key: "gapMode",
      options: [
        "No Gaps",
        "Gap = selection divided by",
        "Gap = selection multiplied by",
        "Gap =",
      ],
    },
    { type: ItemType.editFieldNumerical, key: "gapDivisor" },
    { type: ItemType.editFieldNumerical, key: "gapMultiplier" },
    { type: ItemType.editFieldUnitDependent, key: "gapLength" },
    { type: ItemType.unitsPopup, key: "units" },
  ],

  effect(doc, host, prefs): ModResult {
    if (doc.selSt === doc.selEnd) expandEmptySelection(doc);
    host.setupProgress("Replicating…");

    const frameSize = frameSizeFor(doc.bps);
    const copies = Math.max(1, prefs.copies);
    const doExtend = resolveDoExtend(host);
    let changed = false;

    for (const i of doc.selectedChannelIndices()) {
      const ch = doc.channels[i]!;
      const { start, end } = channelRange(doc, i);
      const selBytes = end - start;
      if (selBytes <= 0) continue;

      let gap = gapBytes(prefs, selBytes);
      gap -= gap % frameSize;

      // The original replicates in place, extending the channel. With extension
      // refused there is nowhere to put the copies.
      if (!doExtend) continue;

      // Copies sit after the original: N copies means N-1 extra + gaps between.
      const extra = (copies - 1) * (selBytes + gap);
      const grown = new Uint8Array(ch.data.length + extra);
      grown.fill(silentByte(doc.bps));
      grown.set(ch.data.subarray(0, end), 0);

      const source = ch.data.subarray(start, end);
      let write = end;
      for (let c = 1; c < copies; c++) {
        write += gap; // gap first — it sits between copies
        grown.set(source, write);
        write += selBytes;
      }
      // Whatever followed the selection is pushed out behind the last copy.
      grown.set(ch.data.subarray(end), write);
      ch.data = grown;
      changed = true;
    }

    host.closeProgress();
    if (!changed) return kModCancel;
    doc.selEnd = doc.maxRelChSize();
    return kModNoError;
  },
};
