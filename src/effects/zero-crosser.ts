/**
 * **Zero Crosser** — SPEC §8 #37. `[DOC+RES]`, not byte-verified.
 *
 * Non-audio: it moves the *selection* to the nearest zero-crossings and never
 * writes a sample. Its group is "Selection", and it is the proof that the module
 * ABI is general enough to host a selection tool — `effect()` may mutate
 * `selSt`/`selEnd` and return, touching nothing else.
 *
 * `[INTERPRETED]`: "nearest zero-crossings" is all the docs give. We take a
 * crossing to be a **sign change between adjacent frames**, with zero counting as
 * positive — the convention Weird Tempo's segmenter provably uses, and the only
 * other place in the app that hunts for crossings. Whether Expand/Shrink search a
 * bounded window, and what happens when no crossing exists, are unrecorded; we
 * leave the edge where it was.
 */

import { frameSizeFor, getSample } from "../samples.js";
import { expandEmptySelection } from "../selection.js";
import { kModCancel, kModNoError, type ModResult } from "../runtime/results.js";
import { ItemType, type SettingsSchema } from "../runtime/schema.js";
import type { EffectModule } from "../runtime/module.js";
import type { SoundDocument } from "../document.js";

export const CrossAction = { expand: 1, shrink: 2 } as const;
export const CrossSide = { left: 1, right: 2, both: 3 } as const;

export interface ZeroCrosserSettings {
  /** 1 = Expand, 2 = Shrink. */
  action: number;
  /** 1 = Left, 2 = Right, 3 = Both. */
  side: number;
}

/** Zero counts as positive — Weird Tempo's convention. */
function isPositive(doc: SoundDocument, data: Uint8Array, p: number): boolean {
  return getSample(data, p, doc.bps) >= 0;
}

/**
 * Search from `from` in `step` direction for the first frame whose sign differs
 * from its predecessor. Returns null if the buffer runs out first.
 */
function findCrossing(
  doc: SoundDocument,
  data: Uint8Array,
  from: number,
  step: number,
  limit: number,
): number | null {
  const frameSize = frameSizeFor(doc.bps);
  let p = from;
  if (p < 0 || p + frameSize > data.length) return null;
  let sign = isPositive(doc, data, p);
  for (;;) {
    const next = p + step * frameSize;
    if (next < 0 || next + frameSize > data.length) return null;
    if (step > 0 ? next >= limit : next < limit) return null;
    const s = isPositive(doc, data, next);
    if (s !== sign) return step > 0 ? next : next + frameSize;
    sign = s;
    p = next;
  }
}

export const ZeroCrosser: EffectModule<ZeroCrosserSettings> = {
  name: "Zero Crosser",
  signature: "ZeroCrosser",
  group: "Selection",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "",

  defaults: () => ({ action: CrossAction.expand, side: CrossSide.both }),

  schema: (): SettingsSchema => [
    { type: ItemType.radioButton, key: "action", options: ["Expand", "Shrink"] },
    { type: ItemType.ignore, label: "the selection to nearest zero-crossings on" },
    { type: ItemType.radioButton, key: "side", options: ["Left", "Right", "Both"] },
    { type: ItemType.ignore, label: "side(s)" },
  ],

  effect(doc, _host, prefs): ModResult {
    expandEmptySelection(doc);
    const idx = doc.firstSelCh - 1;
    const ch = doc.channels[idx];
    if (!ch) return kModCancel;

    const expand = prefs.action === CrossAction.expand;
    const doLeft = prefs.side === CrossSide.left || prefs.side === CrossSide.both;
    const doRight = prefs.side === CrossSide.right || prefs.side === CrossSide.both;
    let { selSt, selEnd } = doc;

    const frameSize = frameSizeFor(doc.bps);

    if (doLeft) {
      // selSt is inclusive, so the search starts on it. Expand hunts outward
      // (left), shrink inward (right).
      const found = expand
        ? findCrossing(doc, ch.data, selSt, -1, 0)
        : findCrossing(doc, ch.data, selSt, 1, selEnd);
      if (found !== null) selSt = found;
    }
    if (doRight) {
      // selEnd is EXCLUSIVE — the last sample in the selection is one frame
      // back, and starting the scan on selEnd itself reads past the buffer.
      const last = selEnd - frameSize;
      const found = expand
        ? findCrossing(doc, ch.data, last, 1, ch.data.length)
        : findCrossing(doc, ch.data, last, -1, selSt);
      if (found !== null) selEnd = found;
    }

    if (selSt === doc.selSt && selEnd === doc.selEnd) return kModCancel;
    if (selEnd < selSt) return kModCancel;
    doc.selSt = selSt;
    doc.selEnd = selEnd;
    // Returns noError having touched no audio at all — the selection IS the product.
    return kModNoError;
  },
};
