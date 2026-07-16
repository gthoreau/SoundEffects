/**
 * **Weird Tempo** — SPEC §8 #36. `[BINARY: disassembly/Weird-Tempo.md]`
 *
 * A zero-crossing-aligned **period doubler**: chop the selection into
 * pseudo-periods at falling zero-crossings and write each one twice. No
 * windowing, no interpolation, no overlap-add — the most naive possible
 * time-stretch. Exact for periodic signals; on real audio the pseudo-periods are
 * irregular, and that judder is the "really weird" the name promises.
 *
 * Three things that look like bugs and are not:
 *   - **Silence and unipolar material pass through unchanged** — no falling
 *     crossing ever fires, so nothing is ever doubled.
 *   - **`doExtend` is ignored entirely.** Weird Tempo always lengthens the sound,
 *     whatever the host preference says.
 *   - **It warns on every single run**, and the warning apologises for itself.
 */

import { frameSizeFor, getSample } from "../samples.js";
import { channelRange, expandEmptySelection } from "../selection.js";
import { kModCancel, kModNoError, type ModResult } from "../runtime/results.js";
import { Progress } from "../runtime/skeleton.js";
import type { EffectModule } from "../runtime/module.js";

export interface WeirdTempoOptions {
  /** Answers the unconditional warning. Returning false = Cancel = no-op. */
  confirm?: () => boolean;
}

/**
 * The segmentation state machine, lifted straight from the disassembly.
 *
 * Two booleans, `wantPos` and `phase`, both start true. A *hit* fires when the
 * sample's sign matches what's sought (`wantPos` ⇒ sample ≥ 0, else sample < 0);
 * each hit toggles both. Every **second** hit ends a segment — i.e. each sample
 * where the signal goes negative after having been non-negative.
 *
 * Zero counts as positive. The first segment is therefore short: it runs from the
 * selection start to the first strictly negative sample after a non-negative one,
 * which is a half-period. Full periods follow.
 *
 * @returns segment end offsets (exclusive), in bytes.
 */
export function segmentBoundaries(
  data: Uint8Array,
  start: number,
  end: number,
  bps: number,
): number[] {
  const frameSize = frameSizeFor(bps);
  const bounds: number[] = [];
  let wantPos = true;
  let phase = true;

  for (let p = start; p < end; p += frameSize) {
    const s = getSample(data, p, bps);
    const isPos = s >= 0; // zero counts as positive
    const hit = wantPos ? isPos : !isPos;
    if (!hit) continue;
    wantPos = !wantPos;
    phase = !phase;
    if (phase) bounds.push(p); // every second hit
  }
  return bounds;
}

export const WeirdTempo: EffectModule<WeirdTempoOptions> = {
  name: "Weird Tempo",
  signature: "WeirdTempo",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Doing weird things to the tempo…",
  strings: [
    "",
    "",
    "Warning: this module is a short hack which may introduce noise. You may try do a Smooth after this to eliminate some noise.",
  ],

  defaults: () => ({}),

  effect(doc, host, prefs): ModResult {
    // On EVERY run, before doing anything: error string #3 with a Cancel button.
    // Not a first-run nag — every time.
    host.showError(3, true);
    if (prefs.confirm && !prefs.confirm()) return kModCancel;

    const frameSize = frameSizeFor(doc.bps);
    host.setupProgress("Doing weird things to the tempo…");
    expandEmptySelection(doc);

    const total = Math.floor(host.getBytesToProcess(doc) / frameSize);
    // Unusually, canCancel is TRUE here — most modules hard-code false.
    const progress = new Progress(host, total, true);

    let result: ModResult = kModCancel;
    let maxSelEnd = 0;
    let cancelled = false;

    for (const index of doc.selectedChannelIndices()) {
      if (cancelled) break; // remaining channels skipped, per the doc
      const ch = doc.channels[index]!;
      const { start, end } = channelRange(doc, index);
      if (start === end) continue;

      const src = ch.data.slice(); // the pristine source; the original works in place
      const bounds = segmentBoundaries(src, start, end, doc.bps);

      // Build the output: everything before the selection, then each complete
      // segment twice, then the trailing partial once.
      const out: number[] = [];
      for (let p = 0; p < start; p++) out.push(src[p]!);

      let segStart = start;
      for (const b of bounds) {
        for (let p = segStart; p < b; p++) out.push(src[p]!);
        for (let p = segStart; p < b; p++) out.push(src[p]!); // the segment twice
        segStart = b;
        if (progress.tick()) {
          cancelled = true;
          break;
        }
      }
      // Trailing partial segment (no falling crossing after it): written once.
      for (let p = segStart; p < end; p++) out.push(src[p]!);
      for (let p = end; p < src.length; p++) out.push(src[p]!);

      ch.data = Uint8Array.from(out);
      const selEnd = out.length - (src.length - end);
      if (selEnd > maxSelEnd) maxSelEnd = selEnd;
      result = kModNoError;
    }

    host.closeProgress();
    doc.selEnd = Math.max(doc.selEnd, maxSelEnd);
    // Cancelling still finalises the channel and returns noErr: the half-processed
    // result is kept AND made structurally consistent. The destructive philosophy,
    // engineered rather than merely tolerated.
    return result;
  },
};
