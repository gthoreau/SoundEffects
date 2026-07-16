/**
 * **Quick Mix** — SPEC §8 #23. `[DOC+RES]`, not byte-verified.
 *
 * Mixes the selected channels down to a New Channel or a New Document, in Mono or
 * Stereo. Stereo mode routes by each channel's **L/R speaker buttons** — the same
 * toggles that sit beside each lane in the wave editor (§3), which is a nice piece
 * of design: the routing you hear while editing is the routing you get when you
 * mix.
 *
 * `[INTERPRETED]`: the mix is a plain sum with a saturating clamp, no averaging or
 * headroom scaling. That matches Sum's behaviour and this app's general refusal to
 * protect you from yourself — but it is inference. "Use each channel's pan" scales
 * by the per-channel volume; whether it also applies the 8.8 `volume` field is
 * unrecorded. See SPEC §14.
 */

import { frameSizeFor, getSample, setSample, silentByte } from "../samples.js";
import { channelRange, expandEmptySelection } from "../selection.js";
import { kModCancel, kModNoError, type ModResult } from "../runtime/results.js";
import { ItemType, type SettingsSchema } from "../runtime/schema.js";
import type { EffectModule } from "../runtime/module.js";

export const MixDest = { newChannel: 1, newDocument: 2 } as const;
export const MixUse = { mono: 1, stereo: 2 } as const;

export interface QuickMixSettings {
  /** 1 = New Channel, 2 = New Document. */
  destination: number;
  /** 1 = Mono, 2 = Stereo. */
  use: number;
  usePan: boolean;
}

export const QuickMix: EffectModule<QuickMixSettings> = {
  name: "Quick Mix",
  signature: "QuickMix",
  version: "1.0",
  author: "Alberto Ricci",
  progressText: "Mixing…",

  defaults: () => ({ destination: MixDest.newChannel, use: MixUse.mono, usePan: false }),

  schema: (): SettingsSchema => [
    { type: ItemType.ignore, label: "Mix to" },
    { type: ItemType.radioButton, key: "destination", options: ["New Channel", "New Document"] },
    { type: ItemType.ignore, label: "Use" },
    { type: ItemType.radioButton, key: "use", options: ["Mono", "Stereo"] },
    { type: ItemType.checkBox, key: "usePan", label: "Use each channel's pan" },
  ],

  effect(doc, host, prefs): ModResult {
    expandEmptySelection(doc);
    host.setupProgress("Mixing…");

    const frameSize = frameSizeFor(doc.bps);
    const { min, max } = host.getSampleValueLimits(doc.bps);
    const indices = doc.selectedChannelIndices();
    if (indices.length === 0) {
      host.closeProgress();
      return kModCancel;
    }

    // The mix spans the longest selected channel; shorter ones contribute zero
    // past their end, as Iterative Crossfade does.
    let frames = 0;
    for (const i of indices) {
      const r = channelRange(doc, i);
      frames = Math.max(frames, Math.floor((r.end - r.start) / frameSize));
    }
    if (frames < 1) {
      host.closeProgress();
      return kModCancel;
    }

    const stereo = prefs.use === MixUse.stereo;
    const outCount = stereo ? 2 : 1;
    const acc = Array.from({ length: outCount }, () => new Float64Array(frames));

    for (const i of indices) {
      const ch = doc.channels[i]!;
      const r = channelRange(doc, i);
      // 8.8 fixed: 0x0100 = 1.0. Values above unity overdrive on purpose.
      const vol = prefs.usePan ? ch.volume / 0x0100 : 1;

      for (let f = 0; f < frames; f++) {
        const p = r.start + f * frameSize;
        if (p + frameSize > r.end) break;
        const s = getSample(ch.data, p, doc.bps) * vol;
        if (!stereo) {
          acc[0]![f]! += s;
        } else {
          // Routed by the lane's own speaker toggles.
          if (ch.outLeft) acc[0]![f]! += s;
          if (ch.outRight) acc[1]![f]! += s;
        }
      }
    }

    const outputs = acc.map((a) => {
      const out = new Uint8Array(frames * frameSize);
      out.fill(silentByte(doc.bps));
      for (let f = 0; f < frames; f++) {
        let v = Math.trunc(a[f]!);
        if (v < min) v = min;
        else if (v > max) v = max;
        setSample(out, f * frameSize, v, doc.bps);
      }
      return out;
    });

    if (prefs.destination === MixDest.newDocument) {
      // ModNewDocument: the host owns the resulting window and picks it up from
      // host.spawnedDocuments. The source document is left untouched.
      const fresh = host.newDocument(doc);
      for (const out of outputs) {
        fresh.addChannel();
        fresh.channels[fresh.numChans - 1]!.data = out;
      }
      host.closeProgress();
      return kModNoError;
    }

    for (const out of outputs) {
      host.addNewChannel(doc);
      doc.channels[doc.numChans - 1]!.data = out;
    }
    host.closeProgress();
    return kModNoError;
  },
};
