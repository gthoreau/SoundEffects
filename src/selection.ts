/**
 * Selection semantics — SPEC §2.4 and §5.
 *
 * The basics are `[SOURCE: Gain.c]`. Select Peak and Select Silence are
 * `[BINARY: disassembly/Select-Silence-Peak.md]` and are ported instruction-for-
 * instruction, quirks included — see the notes at each quirk. This module is
 * pure selection maths: no dialogs, no option-key handling, no redraw. The UI
 * shell (step 6) supplies the parameters those dialogs collect.
 */

import type { SoundDocument } from "./document.js";
import { frameSizeFor, isSilentFrame } from "./samples.js";

/** A resolved byte range for one channel, half-open. */
export interface ByteRange {
  start: number;
  end: number;
}

/**
 * The range a given channel actually processes. `[SOURCE: Gain.c]`
 *
 * Order matters and is not intuitive: clamp to the channel's own length *first*
 * (this is how a short channel gets its own smaller intersection), and only then
 * snap both ends down to a frame boundary.
 */
export function channelRange(
  doc: SoundDocument,
  channelIndex: number,
  selSt = doc.selSt,
  selEnd = doc.selEnd,
): ByteRange {
  const frameSize = doc.frameSize;
  const channelSize = doc.channels[channelIndex]!.data.length;
  let start = selSt;
  let end = selEnd;
  if (start > channelSize) start = channelSize;
  if (end > channelSize) end = channelSize;
  start -= start % frameSize;
  end -= end % frameSize;
  return { start, end };
}

/**
 * "Empty selection means everything." `[SOURCE: Gain.c]`
 *
 * Expands a caret to `[0, maxRelChSize)` — note *rel*: the max over the
 * **selected** channels, not the whole document. Returns the range without
 * touching the document, since effects mutate `modInfo` but callers here
 * usually want to ask rather than commit.
 */
export function resolveSelection(doc: SoundDocument): ByteRange {
  if (doc.selSt === doc.selEnd) return { start: 0, end: doc.maxRelChSize() };
  return { start: doc.selSt, end: doc.selEnd };
}

/** Apply `resolveSelection` to the document, as `effect()` does in-place. */
export function expandEmptySelection(doc: SoundDocument): void {
  if (doc.selSt === doc.selEnd) {
    doc.selSt = 0;
    doc.selEnd = doc.maxRelChSize();
  }
}

/** `GetBytesToProcess(modInfo)` — total bytes across selected channels. `[SOURCE: Glue.h]` */
export function getBytesToProcess(doc: SoundDocument): number {
  const { start, end } = resolveSelection(doc);
  let total = 0;
  for (const i of doc.selectedChannelIndices()) {
    const r = channelRange(doc, i, start, end);
    total += Math.max(0, r.end - r.start);
  }
  return total;
}

/**
 * Set the selection, snapping to frame boundaries (§2.4). Bounds are clamped to
 * the document's longest channel — a selection may legitimately extend past a
 * *short* channel, which is how per-channel clamping stays meaningful.
 */
export function setSelection(doc: SoundDocument, start: number, end: number): void {
  const frameSize = doc.frameSize;
  const limit = doc.maxChSize();
  let s = Math.max(0, Math.min(start, limit));
  let e = Math.max(0, Math.min(end, limit));
  if (e < s) [s, e] = [e, s];
  s -= s % frameSize;
  e -= e % frameSize;
  doc.selSt = s;
  doc.selEnd = e;
}

/** Selection ▸ Set Loop (§2.1). Loop bounds follow the current selection. */
export function setLoop(doc: SoundDocument, start = doc.selSt, end = doc.selEnd): void {
  doc.loopSt = start;
  doc.loopEnd = end;
}

// ---------------------------------------------------------------------------
// Select Exactly… — DITL 140. `[RES/DOC]`, not byte-verified.
// ---------------------------------------------------------------------------

export type ExactlyAnchor = "before" | "centered" | "after";

/**
 * "Select exactly [N units] of sound • Before / • Centered on / • After
 * {the selection | the insertion point}" `[RES: DITL 140]`
 *
 * `[DOC]` — the dialog's items are known from the resource fork but the routine
 * was not disassembled, so the anchoring maths below is a reading of the words,
 * not a port. "Before" ends where the selection starts; "After" begins where it
 * ends; "Centered on" straddles the selection's midpoint. Clamped to the
 * document and frame-snapped like any other selection.
 */
export function selectExactly(
  doc: SoundDocument,
  lengthBytes: number,
  anchor: ExactlyAnchor,
): void {
  const frameSize = doc.frameSize;
  let len = lengthBytes - (lengthBytes % frameSize);
  let start: number;
  switch (anchor) {
    case "before":
      start = doc.selSt - len;
      break;
    case "after":
      start = doc.selEnd;
      break;
    case "centered": {
      const mid = doc.selSt + Math.floor((doc.selEnd - doc.selSt) / 2);
      start = mid - Math.floor(len / 2);
      break;
    }
  }
  setSelection(doc, start, start + len);
}

// ---------------------------------------------------------------------------
// Select Peak — CODE 4 @0x2b90, helper @0x3090. `[BINARY]`
// ---------------------------------------------------------------------------

export type PeakScope = "sound" | "selection";

export interface SelectPeakOptions {
  /** Which radio the dialog had selected. Option-key invocation = "selection". */
  scope: PeakScope;
  /** Already converted to bytes by the units field. Option-key invocation = 0. */
  marginBytes: number;
  /** Function-key single-channel focus (doc+0xda), 1-based; 0 = none. */
  singleChannelFocus?: number;
}

/**
 * The peak metric: **|sum across the selected channels| at each frame position**
 * — not a per-channel max. `[BINARY: CODE 4 @0x3090]`
 *
 * Faithful details that matter:
 * - Channels shorter than `pos` are **skipped**, not padded with silence.
 * - 8-bit sums in a 16-bit register; wider samples sum in 32 bits. Both can wrap
 *   on enough channels, and we let them wrap.
 * - Frames wider than 4 bytes contribute only their first 4 bytes.
 * - Ties go to the **earliest** position (the comparison is strictly greater).
 */
export function findPeak(
  doc: SoundDocument,
  selSt: number,
  selEnd: number,
  firstSelCh: number,
  lastSelCh: number,
): { peakPos: number; metric: number } {
  const frameSize = doc.frameSize;
  const step = frameSize === 1 ? 1 : frameSize;
  const n = Math.min(frameSize, 4);
  let best = 0;
  let peakPos = 0;

  for (let pos = selSt; pos < selEnd; pos += step) {
    let acc = 0;
    for (let c = firstSelCh - 1; c <= lastSelCh - 1; c++) {
      const buf = doc.channels[c]!.data;
      if (pos >= buf.length) continue; // unequal lengths: omitted, not padded
      if (frameSize === 1) {
        acc += ((buf[pos]! ^ 0x80) << 24) >> 24;
      } else {
        let v = 0;
        for (let b = 0; b < n; b++) v = (v << 8) | (buf[pos + b] ?? 0);
        const shift = (4 - n) * 8;
        acc += (v << shift) >> shift;
      }
    }
    // Emulate the accumulator's width: 16-bit for 8-bit audio, 32-bit above.
    const metric = Math.abs(frameSize === 1 ? ((acc << 16) >> 16) : acc | 0);
    if (metric > best) {
      best = metric;
      peakPos = pos;
    }
  }

  if (frameSize > 1) peakPos -= peakPos % frameSize;
  return { peakPos, metric: best };
}

/**
 * Selection ▸ Select Peak… `[BINARY: CODE 4 @0x2b90]`
 *
 * Selects exactly one frame at the peak, then widens by `marginBytes` on each
 * side, each end clamped independently — no re-centering when one end clamps.
 *
 * SPEC §4 note: option-key is **not** "repeat last settings"; it skips the
 * dialog with margin 0 and "the Selection" scope. Only the units popup code is
 * remembered per document, never the margin.
 */
export function selectPeak(doc: SoundDocument, opts: SelectPeakOptions): void {
  const frameSize = doc.frameSize;
  const focus = opts.singleChannelFocus ?? 0;

  // Scoping — which range and channels get searched.
  if (opts.scope === "sound") {
    doc.firstSelCh = 1;
    doc.lastSelCh = doc.numChans;
    doc.selSt = 0;
    doc.selEnd = doc.maxChSize(); // max over ALL channels
  } else if (focus !== 0) {
    doc.firstSelCh = focus;
    doc.lastSelCh = focus;
    doc.selSt = 0;
    doc.selEnd = doc.maxRelChSize();
  } else if (doc.selSt === doc.selEnd) {
    doc.selSt = 0;
    doc.selEnd = doc.maxRelChSize();
  }

  const { peakPos } = findPeak(doc, doc.selSt, doc.selEnd, doc.firstSelCh, doc.lastSelCh);
  const maxSelLen = doc.maxRelChSize();
  const margin = opts.marginBytes;

  let selStart = peakPos;
  let selEnd = peakPos + frameSize;
  if (margin < selStart) selStart -= margin;
  else selStart = 0;
  const room = maxSelLen - selEnd;
  if (room > margin) selEnd += margin;
  else selEnd = maxSelLen;

  doc.selSt = selStart;
  doc.selEnd = selEnd;
}

// ---------------------------------------------------------------------------
// Select Silence — CODE 4 @0x61f8. `[BINARY]`
// ---------------------------------------------------------------------------

/**
 * Selection ▸ Select Silence. `[BINARY: CODE 4 @0x61f8]`
 *
 * Silence is **exact digital zero** — no threshold, no tolerance, no RMS window.
 * One sample of ±1 ends the run. Requires an insertion point; with a real
 * selection the command silently does nothing.
 *
 * Scans out from the caret in each selected channel, leaves one frame of silence
 * unselected as margin at each silent/non-silent boundary (dropped where silence
 * reaches a channel edge), then **intersects** the per-channel runs.
 *
 * @returns true if the selection changed.
 */
export function selectSilence(doc: SoundDocument): boolean {
  if (doc.selSt !== doc.selEnd) return false; // insertion point required
  const frameSize = doc.frameSize;
  const caret = doc.selSt;

  // Intersection accumulators.
  let selSt = 0;
  let selEnd = doc.maxRelChSize();

  for (const c of doc.selectedChannelIndices()) {
    const buf = doc.channels[c]!.data;
    const size = buf.length;

    let w = caret;
    let adjusted = false;
    if (w === size) {
      w -= frameSize;
      adjusted = true;
    }
    if (size <= w) continue; // channel too short: skipped

    // --- Forward scan (right edge) ---
    let q = w;
    if (frameSize === 1) {
      while (q < size && buf[q] === 0x80) q++;
    } else {
      while (q < size && buf[q] === 0x00) q++;
      q -= q % frameSize; // a frame is silent only if all its bytes are zero
    }
    let endChan = q - frameSize; // exclude the last whole silent frame: margin
    if (endChan < w) endChan = w; // caret not in silence → collapses
    if (adjusted) endChan += frameSize;

    // Buffer edge: silence running to the channel end takes no margin.
    if (endChan === size - frameSize && size >= frameSize) {
      let lastSilent: boolean;
      if (frameSize === 1) {
        lastSilent = buf[size - 1] === 0x80;
      } else {
        // Faithful quirk: the original walks frameSize bytes *downward* from the
        // last frame's first byte rather than upward across the last frame, so
        // the test straddles the previous frame's tail. Affects only this frame.
        lastSilent = true;
        for (let i = 0; i < frameSize; i++) {
          if (buf[size - frameSize - i] !== 0x00) {
            lastSilent = false;
            break;
          }
        }
      }
      if (lastSilent) endChan = size;
    }

    // --- Backward scan (left edge) ---
    let p = w;
    if (frameSize === 1) {
      while (p >= 0 && buf[p] === 0x80) p--;
    } else {
      while (p >= 0 && buf[p] === 0x00) p--;
      p += p % frameSize; // C-style signed remainder; JS % matches (-1 % 2 === -1)
    }
    let startChan = p + 2 * frameSize; // first silent frame left out: margin

    // Inversion guard: caret wasn't inside silence → no-op for this channel.
    if (endChan < startChan) {
      startChan = caret;
      endChan = caret;
    }
    // The left margin may never push the start past the caret.
    if (startChan > caret + frameSize) startChan = caret;
    // Buffer edge: silence running to the channel start takes no margin.
    if (startChan === frameSize && size >= frameSize && isSilentFrame(buf, 0, doc.bps)) {
      startChan = 0;
    }

    selSt = Math.max(selSt, startChan);
    selEnd = Math.min(selEnd, endChan);
  }

  // "Silence found but empty after intersection" still moves the caret; only an
  // exact no-change is reported as no-op.
  if (selSt === caret && selEnd === caret) return false;
  doc.selSt = selSt;
  doc.selEnd = selEnd;
  return true;
}

/** Frame size for a bps, re-exported so callers need not reach into samples.ts. */
export { frameSizeFor };
