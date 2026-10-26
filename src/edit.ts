/**
 * Destructive editing — SPEC §5.
 *
 * Everything here rewrites buffers in place. **There is no undo and there must
 * never be one** (§1): ⌘Z stays greyed forever. Callers do not get a snapshot
 * back, and nothing in this module keeps one.
 *
 * Confidence: the editing engine is `[DOC/RES]` — the behaviour is known from
 * the manuals and resource fork, but these routines were **not** disassembled —
 * except Shift Sound… and Leave Silence…, which are `[BINARY]` (see their notes).
 * The core weirdness (channels are independent buffers, so an edit shortens only
 * the channels it touches and lets the rest drift out of alignment) is well
 * attested; the finer policy calls are flagged `[INTERPRETED]` at each site and
 * are listed as open items in SPEC §14.
 *
 * A deliberate distinction from effects: **an empty selection is NOT "everything"
 * here.** `Gain.c` expands a caret to the whole channel because an effect with
 * nothing selected should process the sound; a *Clear* with nothing selected must
 * not wipe the document. Menu edits treat a caret as an insertion point.
 */

import { SoundDocument, type Channel } from "./document.js";
import { silentByte } from "./samples.js";
import { channelRange } from "./selection.js";

/** Bytes the sound is allowed to grow by, if a host wants to cap it. */
export interface EditLimits {
  maxBytesPerChannel?: number;
}

function fillSilence(data: Uint8Array, start: number, end: number, bps: number): void {
  data.fill(silentByte(bps), start, end);
}

/** Splice a range out of a channel, returning a shorter buffer. */
function spliceOut(ch: Channel, start: number, end: number): void {
  const len = end - start;
  if (len <= 0) return;
  const out = new Uint8Array(ch.data.length - len);
  out.set(ch.data.subarray(0, start), 0);
  out.set(ch.data.subarray(end), start);
  ch.data = out;
}

/** Splice bytes into a channel at `at`, growing it. */
function spliceIn(ch: Channel, at: number, bytes: Uint8Array): void {
  const out = new Uint8Array(ch.data.length + bytes.length);
  out.set(ch.data.subarray(0, at), 0);
  out.set(bytes, at);
  out.set(ch.data.subarray(at), at + bytes.length);
  ch.data = out;
}

// ---------------------------------------------------------------------------
// Clear / Cut / Copy / Paste
// ---------------------------------------------------------------------------

/**
 * Edit ▸ Clear — "removes the selected range from the selected channels only —
 * since channels are independent buffers, this shortens *only those channels*,
 * letting tracks drift out of alignment. This is a core weirdness." (§5)
 *
 * A caret selects nothing, so Clear does nothing. Each channel clears its own
 * clamped-and-snapped intersection (`Gain.c` rules), so a channel shorter than
 * the selection loses only what it has.
 *
 * @returns true if any channel changed.
 */
export function clear(doc: SoundDocument): boolean {
  if (doc.selSt === doc.selEnd) return false;
  let changed = false;
  for (const i of doc.selectedChannelIndices()) {
    const { start, end } = channelRange(doc, i);
    if (end > start) {
      spliceOut(doc.channels[i]!, start, end);
      changed = true;
    }
  }
  if (changed) doc.selEnd = doc.selSt; // collapse to a caret at the cut point
  return changed;
}

/**
 * Edit ▸ Copy — "Cut/Copy put actual sound (with its rate/bps) on the
 * clipboard." (§5) The clipboard is a real sound, playable in its own window,
 * so it is a `SoundDocument`, not a byte blob.
 *
 * Only the selected channels are copied, each contributing its own clamped
 * intersection — so copying an unequal-length region yields an unequal-length
 * clipboard. That is correct, not a bug to round off.
 *
 * @returns null if there is nothing selected.
 */
export function copy(doc: SoundDocument): SoundDocument | null {
  if (doc.selSt === doc.selEnd) return null;
  const clip = new SoundDocument({
    name: "Clipboard",
    rate: doc.rate,
    bps: doc.bps,
    baseFreq: doc.baseFreq,
    numChans: 0,
  });
  for (const i of doc.selectedChannelIndices()) {
    const src = doc.channels[i]!;
    const { start, end } = channelRange(doc, i);
    clip.channels.push({
      data: src.data.slice(start, Math.max(start, end)),
      outLeft: src.outLeft,
      outRight: src.outRight,
      volume: src.volume,
    });
  }
  if (clip.channels.length === 0) return null;
  clip.firstSelCh = 1;
  clip.lastSelCh = clip.numChans;
  clip.selSt = 0;
  clip.selEnd = clip.maxChSize();
  return clip;
}

/** Edit ▸ Cut — copy, then clear. */
export function cut(doc: SoundDocument): SoundDocument | null {
  const clip = copy(doc);
  if (clip) clear(doc);
  return clip;
}

/** What a paste would have to reconcile. `[RES: ALRT 152/166]` */
export interface PasteMismatch {
  bps?: { from: number; to: number };
  rate?: { from: number; to: number };
}

/**
 * Does this paste need the resample prompt? (§5)
 *
 * "You are trying to paste a sound sampled at ^1 bits into a sound sampled at ^2
 * bits. Do you want me to resample the sound you are inserting at ^3 bits?" —
 * Resample / Cancel. The prompt is the UI's job; this just reports the mismatch.
 *
 * Resampling itself is `ModResampleSoundBits` / `ModResampleSoundRate`, host DSP
 * recovered in `disassembly/Host-Resamplers.md` but not yet ported — it lands
 * with the effects runtime (step 3). Until then a mismatched paste can only go
 * in raw, which is the app's "just lie about it" path and mangles pitch/format
 * exactly as advertised.
 */
export function pasteMismatch(doc: SoundDocument, clip: SoundDocument): PasteMismatch | null {
  const m: PasteMismatch = {};
  if (clip.bps !== doc.bps) m.bps = { from: clip.bps, to: doc.bps };
  if (clip.rate !== doc.rate) m.rate = { from: clip.rate, to: doc.rate };
  return m.bps || m.rate ? m : null;
}

/**
 * Edit ▸ Paste. `[INTERPRETED]`
 *
 * Inserts the clipboard's channels at the caret, one clipboard channel per
 * document channel starting at `firstSelCh`. A non-empty selection is replaced
 * (cleared first), matching the text-editor model §2.4 invokes for the caret.
 *
 * Unverified policy, needs disassembly (SPEC §14): what happens when the
 * clipboard has a different channel count than the paste target. We paste into
 * the channels available from `firstSelCh` and **drop** any surplus clipboard
 * channels rather than inventing new ones — appending channels is a visible,
 * hard-to-undo act and the resource fork gives no alert for it. Channels not
 * receiving audio are left untouched (so they drift, per §5).
 *
 * Does not check bps/rate — call `pasteMismatch` first and resolve it.
 *
 * @returns the number of channels that received audio.
 */
export function paste(doc: SoundDocument, clip: SoundDocument): number {
  if (doc.selSt !== doc.selEnd) clear(doc);
  const at = doc.selSt;
  let pasted = 0;
  let maxLen = 0;
  for (let j = 0; j < clip.numChans; j++) {
    const target = doc.firstSelCh - 1 + j;
    if (target > doc.numChans - 1) break; // surplus clipboard channels dropped
    const dst = doc.channels[target]!;
    const src = clip.channels[j]!.data;
    // A channel shorter than the caret is padded to the caret first — otherwise
    // there is no buffer to insert into.
    if (dst.data.length < at) {
      const grown = new Uint8Array(at);
      grown.set(dst.data, 0);
      fillSilence(grown, dst.data.length, at, doc.bps);
      dst.data = grown;
    }
    spliceIn(dst, at, src);
    if (src.length > maxLen) maxLen = src.length;
    pasted++;
  }
  if (pasted > 0) {
    doc.selSt = at;
    doc.selEnd = at + maxLen;
  }
  return pasted;
}

// ---------------------------------------------------------------------------
// Crop / Silence / Insert
// ---------------------------------------------------------------------------

/**
 * Edit ▸ Crop — "keep selection, discard the rest." (§5) `[INTERPRETED]`
 *
 * Applied to the selected channels only, consistent with Clear. Unselected
 * channels keep their full length — which leaves them longer than the cropped
 * ones. That is the same drift Clear produces and is consistent with the model,
 * but the exact whole-channel-vs-range behaviour of the shipped code is `[ASM]`
 * (§5 flags this explicitly).
 */
export function crop(doc: SoundDocument): boolean {
  if (doc.selSt === doc.selEnd) return false;
  let changed = false;
  for (const i of doc.selectedChannelIndices()) {
    const ch = doc.channels[i]!;
    const { start, end } = channelRange(doc, i);
    ch.data = ch.data.slice(start, Math.max(start, end));
    changed = true;
  }
  if (changed) {
    doc.selSt = 0;
    doc.selEnd = doc.maxRelChSize();
  }
  return changed;
}

/**
 * `ModSilenceOut(modData, chSt, chEnd, st, end)` — zero a range without
 * shortening anything. `[SOURCE: Glue.h]` Channel bounds are 1-based.
 *
 * Writes the depth's silent byte (0x80 for 8-bit, 0x00 above), per channel with
 * the usual clamp-then-snap.
 */
export function silenceOut(
  doc: SoundDocument,
  chSt: number,
  chEnd: number,
  st: number,
  end: number,
): void {
  for (let i = chSt - 1; i <= chEnd - 1; i++) {
    if (i < 0 || i >= doc.numChans) continue;
    const { start, end: e } = channelRange(doc, i, st, end);
    if (e > start) fillSilence(doc.channels[i]!.data, start, e, doc.bps);
  }
}

/** Silence the current selection in the selected channels. */
export function silenceSelection(doc: SoundDocument): void {
  silenceOut(doc, doc.firstSelCh, doc.lastSelCh, doc.selSt, doc.selEnd);
}

/** Insert `bytes` of silence at the caret in every selected channel. */
export function insertSilence(doc: SoundDocument, bytes: number): void {
  const frameSize = doc.frameSize;
  const len = bytes - (bytes % frameSize);
  if (len <= 0) return;
  const at = doc.selSt;
  for (const i of doc.selectedChannelIndices()) {
    const ch = doc.channels[i]!;
    const pad = new Uint8Array(len);
    pad.fill(silentByte(doc.bps));
    const insertAt = Math.min(at, ch.data.length);
    spliceIn(ch, insertAt, pad);
  }
  doc.selSt = at;
  doc.selEnd = at + len;
}

// ---------------------------------------------------------------------------
// Shift Sound… / Leave Silence… — [BINARY: disassembly/Edit-Selection-Commands.md]
// ---------------------------------------------------------------------------

/** The leading run of exact digital zero (as Select Silence), frame-aligned. */
function leadingSilence(data: Uint8Array, frameSize: number): number {
  let run = 0;
  if (frameSize === 1) {
    while (run < data.length && data[run] === 0x80) run++;
  } else {
    while (run < data.length && data[run] === 0x00) run++;
    run -= run % frameSize; // a frame is silent only if all its bytes are zero
  }
  return run;
}

function silence(len: number, bps: number): Uint8Array {
  const out = new Uint8Array(len);
  out.fill(silentByte(bps));
  return out;
}

function concat(a: Uint8Array, b: Uint8Array): Uint8Array {
  const out = new Uint8Array(a.length + b.length);
  out.set(a, 0);
  out.set(b, a.length);
  return out;
}

/** What Shift Sound… acts on. The dialog offers no choice: the scope follows from the
 * selection, and its sentence says which it is. `[BINARY: CODE 12 @0x0ae2]` */
export interface ShiftScope {
  kind: "sound" | "channel" | "channels" | "selection";
  firstCh: number; // 1-based, inclusive
  lastCh: number;
  start: number; // byte range, half-open
  end: number;
}

/**
 * The scope Shift Sound… takes, read from the selection exactly as the original does:
 * - a caret, or a selection spanning the whole sound, with every channel selected:
 *   **the whole sound**, `[0, maxChSize)`;
 * - otherwise a caret, or a selection spanning `[0, maxRelChSize)`: **channel N** or
 *   **channels N to M** (the selected ones), `[0, maxRelChSize)`;
 * - otherwise **the selection**, `[selSt, selEnd)` on the selected channels.
 * (0.9.2's function-key single-channel focus, which also chose "channel N", has no
 * counterpart here.)
 */
export function shiftScope(doc: SoundDocument): ShiftScope {
  const caret = doc.selSt === doc.selEnd;
  const all = doc.firstSelCh === 1 && doc.lastSelCh === doc.numChans;
  if ((caret || (doc.selSt === 0 && doc.selEnd === doc.maxChSize())) && all)
    return { kind: "sound", firstCh: 1, lastCh: doc.numChans, start: 0, end: doc.maxChSize() };
  if (caret || (doc.selSt === 0 && doc.selEnd === doc.maxRelChSize()))
    return {
      kind: doc.firstSelCh === doc.lastSelCh ? "channel" : "channels",
      firstCh: doc.firstSelCh,
      lastCh: doc.lastSelCh,
      start: 0,
      end: doc.maxRelChSize(),
    };
  return { kind: "selection", firstCh: doc.firstSelCh, lastCh: doc.lastSelCh, start: doc.selSt, end: doc.selEnd };
}

/** The dialog's sentence, from STR# 139: "Shift ", the scope, " by:". */
export function shiftCaption(scope: ShiftScope): string {
  switch (scope.kind) {
    case "sound":
      return "Shift the whole sound by:";
    case "channel":
      return `Shift channel number ${scope.firstCh} by:`;
    case "channels":
      return `Shift channels ${scope.firstCh} to ${scope.lastCh} by:`;
    case "selection":
      return "Shift the selection by:";
  }
}

/**
 * Edit ▸ Shift Sound… `[BINARY: CODE 12 @0x0958, worker @0x1042]`
 *
 * Moves the scope's byte range of each scoped channel by `bytes`, and the selection
 * moves with it. Nothing is rotated and lengths are NOT preserved:
 *
 * **Shift Left.** If the range starts less than `bytes` from the beginning, EVERY
 * non-empty channel of the sound (scoped or not) first gets the shortfall of silence
 * prepended, which keeps them aligned. Then the range is copied `bytes` earlier,
 * overwriting what was there. A range that runs to its channel's end shortens the
 * channel (by `bytes`, or by the range's length if that is shorter); otherwise the
 * vacated tail of the range (or the whole old range, if `bytes` exceeds it) is silenced.
 *
 * **Shift Right.** The range is copied `bytes` later, overwriting what follows it; a
 * channel too short to receive it is first lengthened with silence. The vacated head of
 * the range (or the whole old range, if `bytes` exceeds it) is silenced. A channel the
 * range does not reach is skipped.
 *
 * @returns false when there is nothing to shift by.
 */
export function shiftSound(doc: SoundDocument, bytes: number, direction: "left" | "right"): boolean {
  const frameSize = doc.frameSize;
  const amount = bytes - (bytes % frameSize);
  if (amount <= 0) return false;
  const scope = shiftScope(doc);
  let { start, end } = scope;
  const silent = silentByte(doc.bps);

  if (direction === "left") {
    if (start < amount) {
      const grow = amount - start;
      for (const ch of doc.channels) {
        if (ch.data.length !== 0) ch.data = concat(silence(grow, doc.bps), ch.data);
      }
      start += grow;
      end += grow;
      doc.selSt += grow;
      doc.selEnd += grow;
    }
    for (let i = scope.firstCh - 1; i <= scope.lastCh - 1; i++) {
      const ch = doc.channels[i]!;
      const size = ch.data.length;
      const s = Math.min(start, size);
      const e = Math.min(end, size);
      const len = e - s;
      ch.data.copyWithin(s - amount, s, e);
      if (e === size) {
        ch.data = ch.data.slice(0, size - (len > amount ? amount : len));
      } else if (len > amount) {
        ch.data.fill(silent, e - amount, e);
      } else {
        ch.data.fill(silent, s, e);
      }
    }
    doc.selSt -= amount;
    doc.selEnd -= amount;
  } else {
    for (let i = scope.firstCh - 1; i <= scope.lastCh - 1; i++) {
      const ch = doc.channels[i]!;
      const size = ch.data.length;
      const s = Math.min(start, size);
      const e = Math.min(end, size);
      if (s === e) continue;
      if (e + amount > size) ch.data = concat(ch.data, silence(e + amount - size, doc.bps));
      ch.data.copyWithin(s + amount, s, e);
      ch.data.fill(silent, s, s + amount < e ? s + amount : e);
    }
    doc.selSt += amount;
    doc.selEnd += amount;
  }
  return true;
}

export type LeaveSilenceScope = "sound" | "selectedChannels";

/**
 * Edit ▸ Leave Silence… — "Leave [N units] of silence at the beginning of (• the Sound /
 * • the Selected Channels)" `[BINARY: CODE 12 @0x0162]`
 *
 * "the Sound" selects every channel first (the dialog writes the range into the
 * document). Then, over the selected channels:
 *
 * 1. Each channel's leading silent run is measured (exact digital zero). A channel
 *    that is silent from end to end (or empty) is **emptied**.
 * 2. The **shortest** run among the others is the one adjusted, for all of them alike:
 *    every channel loses or gains the same amount at its start, so they stay aligned
 *    and a channel that started later keeps its extra silence. With no audible channel
 *    at all the run counts as 0 and the selection collapses to 0.
 * 3. An emptied channel becomes exactly N of silence.
 *
 * The selection moves with the audio: by the growth if it ends past the old run (else
 * it becomes a caret at N); by the shrink otherwise, each end stopping at 0.
 */
export function leaveSilence(doc: SoundDocument, bytes: number, scope: LeaveSilenceScope): void {
  const frameSize = doc.frameSize;
  const want = bytes - (bytes % frameSize);
  if (scope === "sound") {
    doc.firstSelCh = 1;
    doc.lastSelCh = doc.numChans;
  }

  let run = doc.maxChSize();
  let audible = false;
  for (const i of doc.selectedChannelIndices()) {
    const ch = doc.channels[i]!;
    const lead = leadingSilence(ch.data, frameSize);
    if (run > lead && lead !== ch.data.length) {
      run = lead;
      audible = true;
    } else if (lead === ch.data.length) {
      ch.data = new Uint8Array(0);
    }
  }
  if (!audible) {
    run = 0;
    doc.selSt = 0;
    doc.selEnd = 0;
  }

  for (const i of doc.selectedChannelIndices()) {
    const ch = doc.channels[i]!;
    if (ch.data.length === 0) ch.data = silence(want, doc.bps);
    else if (run < want) ch.data = concat(silence(want, doc.bps), ch.data.subarray(run));
    else if (run > want) ch.data = concat(ch.data.slice(0, want), ch.data.subarray(run));
  }

  if (run < want) {
    const delta = want - run;
    if (run < doc.selEnd) {
      doc.selSt += delta;
      doc.selEnd += delta;
    } else {
      doc.selSt = want;
      doc.selEnd = want;
    }
  } else {
    const delta = run - want;
    doc.selSt = delta < doc.selSt ? doc.selSt - delta : 0;
    doc.selEnd = delta < doc.selEnd ? doc.selEnd - delta : 0;
  }
}
