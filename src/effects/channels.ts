/**
 * The channel-math modules — **Sum**, **Difference**, **Selection to Channel**.
 * SPEC §8 #33/#5/#29. `[DOC]`, not byte-verified.
 *
 * These are the modules that exercise the unequal-channel model hardest: they
 * read two channels of possibly different lengths and write a third.
 */

import { frameSizeFor, getSample, setSample, silentByte } from "../samples.js";
import { channelRange, expandEmptySelection } from "../selection.js";
import { kModCancel, kModNoError, type ModResult } from "../runtime/results.js";
import { ItemType, type SettingsSchema } from "../runtime/schema.js";
import type { EffectModule } from "../runtime/module.js";
import type { SoundDocument } from "../document.js";
import type { Host } from "../runtime/host.js";

/**
 * `ch2' = ch1 ± ch2` over the two selected channels.
 *
 * Where one channel has run out, it contributes digital zero rather than being
 * skipped. That matches Iterative Crossfade's confirmed zero-padding
 * (`Sample-Hose-Iterative-Crossfade.md`) rather than Select Peak's skip-short-
 * channels rule — the two conventions genuinely differ in this app, and mixing
 * them up would be easy. `[DOC]`: unverified for these modules.
 */
function combine(
  doc: SoundDocument,
  host: Host,
  op: (a: number, b: number) => number,
  text: string,
): ModResult {
  if (doc.lastSelCh - doc.firstSelCh + 1 < 2) return kModCancel;

  expandEmptySelection(doc);
  host.setupProgress(text);

  const frameSize = frameSizeFor(doc.bps);
  const { min, max } = host.getSampleValueLimits(doc.bps);
  const aIdx = doc.firstSelCh - 1;
  const bIdx = doc.firstSelCh; // the second selected channel

  const a = doc.channels[aIdx]!;
  const b = doc.channels[bIdx]!;
  const ra = channelRange(doc, aIdx);
  const rb = channelRange(doc, bIdx);

  // The output spans the longer of the two ranges; the shorter zero-pads.
  const frames = Math.max(
    Math.floor((ra.end - ra.start) / frameSize),
    Math.floor((rb.end - rb.start) / frameSize),
  );

  for (let i = 0; i < frames; i++) {
    const pa = ra.start + i * frameSize;
    const pb = rb.start + i * frameSize;
    const va = pa + frameSize <= ra.end ? getSample(a.data, pa, doc.bps) : 0;
    const vb = pb + frameSize <= rb.end ? getSample(b.data, pb, doc.bps) : 0;
    if (pb + frameSize > rb.end) continue; // nowhere to write

    let v = op(va, vb);
    if (v < min) v = min;
    else if (v > max) v = max;
    setSample(b.data, pb, v, doc.bps);
  }

  host.closeProgress();
  return kModNoError;
}

/** **Sum** — `ch2' = ch1 + ch2` for two selected channels. §8 #33. `[DOC]` */
export const Sum: EffectModule = {
  name: "Sum",
  signature: "Sum",
  group: "Misc",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Summing…",
  strings: ["Sorry, man. I haven't got any options."],
  effect(doc, host) {
    return combine(doc, host, (a, b) => a + b, "Summing…");
  },
};

export interface DifferenceSettings {
  /** 1 = New Channel, 2 = New Document. */
  destination: number;
  normalize: boolean;
}

export const DifferenceDest = { newChannel: 1, newDocument: 2 } as const;

/**
 * **Difference** — `ch2' = ch1 − ch2` conceptually. §8 #5. `[DOC+RES]`
 *
 * Requires exactly 2 selected channels; with more it asks "subtract the second
 * selected channel from the first one?". Output goes to a New Channel or a New
 * Document, with an optional Normalize. Has balloon-help strings (`STR# 1100`).
 *
 * Unlike Sum, Difference does **not** overwrite a source channel — the whole point
 * is comparing two takes, so destroying one of them would defeat it. The result
 * lands in a new channel, or in a new document the host picks up from
 * `host.spawnedDocuments`.
 */
export const Difference: EffectModule<DifferenceSettings> = {
  name: "Difference",
  signature: "Difference",
  group: "Misc",
  version: "1.1.1",
  author: "Alberto Ricci",
  progressText: "Subtracting…",
  defaults: () => ({ destination: DifferenceDest.newChannel, normalize: false }),
  schema: (): SettingsSchema => [
    { type: ItemType.radioButton, key: "destination", options: ["New Channel", "New Document"] },
    { type: ItemType.checkBox, key: "normalize", label: "Normalize the difference" },
  ],

  effect(doc, host, prefs): ModResult {
    if (doc.lastSelCh - doc.firstSelCh + 1 < 2) return kModCancel;

    expandEmptySelection(doc);
    host.setupProgress("Subtracting…");

    const frameSize = frameSizeFor(doc.bps);
    const { min, max } = host.getSampleValueLimits(doc.bps);
    const aIdx = doc.firstSelCh - 1;
    const bIdx = doc.firstSelCh;
    const a = doc.channels[aIdx]!;
    const b = doc.channels[bIdx]!;
    const ra = channelRange(doc, aIdx);
    const rb = channelRange(doc, bIdx);

    const frames = Math.max(
      Math.floor((ra.end - ra.start) / frameSize),
      Math.floor((rb.end - rb.start) / frameSize),
    );
    if (frames < 1) {
      host.closeProgress();
      return kModCancel;
    }

    const diff = new Int32Array(frames);
    let peak = 0;
    for (let i = 0; i < frames; i++) {
      const pa = ra.start + i * frameSize;
      const pb = rb.start + i * frameSize;
      const va = pa + frameSize <= ra.end ? getSample(a.data, pa, doc.bps) : 0;
      const vb = pb + frameSize <= rb.end ? getSample(b.data, pb, doc.bps) : 0;
      const v = va - vb;
      diff[i] = v;
      const mag = Math.abs(v);
      if (mag > peak) peak = mag;
    }

    // "Normalize the difference" — the usual reason to run this is to see how
    // small the difference is, so scaling it up to full scale is the point.
    const gain = prefs.normalize && peak > 0 ? Math.trunc((max * 100) / peak) : 100;

    const out = new Uint8Array(frames * frameSize);
    out.fill(silentByte(doc.bps));
    for (let i = 0; i < frames; i++) {
      let v = gain === 100 ? diff[i]! : Math.trunc((diff[i]! * gain) / 100);
      if (v < min) v = min;
      else if (v > max) v = max;
      setSample(out, i * frameSize, v, doc.bps);
    }

    if (prefs.destination === DifferenceDest.newDocument) {
      const fresh = host.newDocument(doc);
      fresh.addChannel();
      fresh.channels[0]!.data = out;
    } else {
      host.addNewChannel(doc);
      doc.channels[doc.numChans - 1]!.data = out;
    }

    host.closeProgress();
    return kModNoError;
  },
};

/**
 * **Selection to Channel** — copies the selection out into new channel(s).
 * §8 #29. `[DOC]`
 *
 * One new channel per selected channel, appended, each carrying that channel's
 * own clamped intersection — so copying an unequal-length selection yields
 * unequal-length new channels.
 */
export const SelectionToChannel: EffectModule = {
  name: "Selection to Channel",
  signature: "SelectionToChannel",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Copying…",
  strings: ["Sorry, man. I haven't got any options."],
  effect(doc, host) {
    expandEmptySelection(doc);
    host.setupProgress("Copying…");

    const slices: Uint8Array[] = [];
    for (const i of doc.selectedChannelIndices()) {
      const { start, end } = channelRange(doc, i);
      slices.push(doc.channels[i]!.data.slice(start, Math.max(start, end)));
    }
    if (slices.length === 0) {
      host.closeProgress();
      return kModCancel;
    }

    const silent = silentByte(doc.bps);
    for (const slice of slices) {
      host.addNewChannel(doc);
      const ch = doc.channels[doc.numChans - 1]!;
      ch.data = slice.length > 0 ? slice : new Uint8Array(0);
      if (ch.data.length === 0) ch.data.fill(silent);
    }

    host.closeProgress();
    return kModNoError;
  },
};
