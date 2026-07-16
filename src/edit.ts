/**
 * Destructive editing — SPEC §5.
 *
 * Everything here rewrites buffers in place. **There is no undo and there must
 * never be one** (§1): ⌘Z stays greyed forever. Callers do not get a snapshot
 * back, and nothing in this module keeps one.
 *
 * Confidence: the editing engine is `[DOC/RES]` — the behaviour is known from
 * the manuals and resource fork, but these routines were **not** disassembled.
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
// Shift Sound… / Leave Silence…  — [RES]-only, interpreted. See SPEC §14.
// ---------------------------------------------------------------------------

export type ShiftScope =
  | { kind: "sound" }
  | { kind: "channel"; channel: number }
  | { kind: "channels"; first: number; last: number }
  | { kind: "selection" };

function scopeChannels(doc: SoundDocument, scope: ShiftScope): number[] {
  switch (scope.kind) {
    case "sound":
      return doc.channels.map((_, i) => i);
    case "channel":
      return [scope.channel - 1];
    case "channels": {
      const out: number[] = [];
      for (let i = scope.first - 1; i <= scope.last - 1; i++) out.push(i);
      return out;
    }
    case "selection":
      return doc.selectedChannelIndices();
  }
}

/**
 * Edit ▸ Shift Sound… — "Shift {the whole sound / channel N / channels N to M /
 * the selection} by [N units], • Shift Left / • Shift Right"
 * `[RES: DITL 139 + STR# 139]` — **`[INTERPRETED]`, needs disassembly (§14).**
 *
 * Read as: slide the audio within its buffer, preserving channel length, filling
 * the vacated end with silence and letting the far end fall off. The alternative
 * readings (rotate; or grow the buffer) are not supported by any string in the
 * resource fork, and length-preserving-with-silence is the only one that makes
 * "Shift Left" lossy in the way a destructive editor implies.
 *
 * For `selection` scope the shift is confined to the selected range.
 */
export function shiftSound(
  doc: SoundDocument,
  scope: ShiftScope,
  bytes: number,
  direction: "left" | "right",
): void {
  const frameSize = doc.frameSize;
  const amount = bytes - (bytes % frameSize);
  if (amount <= 0) return;

  for (const i of scopeChannels(doc, scope)) {
    if (i < 0 || i >= doc.numChans) continue;
    const ch = doc.channels[i]!;
    let lo = 0;
    let hi = ch.data.length;
    if (scope.kind === "selection") {
      const r = channelRange(doc, i);
      lo = r.start;
      hi = r.end;
    }
    const span = hi - lo;
    if (span <= 0) continue;
    if (amount >= span) {
      fillSilence(ch.data, lo, hi, doc.bps);
      continue;
    }
    const region = ch.data.slice(lo, hi);
    if (direction === "left") {
      ch.data.set(region.subarray(amount), lo);
      fillSilence(ch.data, hi - amount, hi, doc.bps);
    } else {
      ch.data.set(region.subarray(0, span - amount), lo + amount);
      fillSilence(ch.data, lo, lo + amount, doc.bps);
    }
  }
}

export type LeaveSilenceScope = "sound" | "selectedChannels";

/**
 * Edit ▸ Leave Silence… — "Leave [N units] of silence at the beginning of
 * (• the Sound / • the Selected Channels)" — trims everything else
 * `[RES: DITL 142]` — **`[INTERPRETED]`, needs disassembly (§14).**
 *
 * Read as: normalise the *leading silent run* to exactly N bytes. Longer runs are
 * trimmed ("trims everything else"); a run shorter than N is padded up to N,
 * which the wording does not clearly license but which is the only way the
 * command can honour "leave N" on a sound that starts abruptly. If the
 * disassembly later shows it only ever trims, drop the padding branch.
 *
 * Leading silence is measured per channel with the same exact-zero test as
 * Select Silence.
 */
export function leaveSilence(
  doc: SoundDocument,
  bytes: number,
  scope: LeaveSilenceScope,
): void {
  const frameSize = doc.frameSize;
  const want = bytes - (bytes % frameSize);
  const indices =
    scope === "sound" ? doc.channels.map((_, i) => i) : doc.selectedChannelIndices();
  const silent = silentByte(doc.bps);

  for (const i of indices) {
    const ch = doc.channels[i]!;
    // Measure the leading silent run, frame-aligned.
    let run = 0;
    while (run < ch.data.length && ch.data[run] === silent) run++;
    run -= run % frameSize;

    if (run > want) {
      spliceOut(ch, want, run);
    } else if (run < want) {
      const pad = new Uint8Array(want - run);
      pad.fill(silent);
      spliceIn(ch, 0, pad);
    }
  }
  doc.selSt = 0;
  doc.selEnd = 0;
}
