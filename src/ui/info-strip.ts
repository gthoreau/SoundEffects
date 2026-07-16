/**
 * The document window's info strip — SPEC §3 zone 1, toggles from §9 page 2.
 *
 * Five fields, each individually switchable in Preferences: rate+size, channel
 * count, file size, and the selected channel's length in samples and in
 * seconds. The formatting is copied from §3's own examples ("22.050 kHz; 16
 * bits", "Mono"/"Stereo"/"N channels") because those examples are the only
 * record of it.
 *
 * The one that carries weight is **`selectedChannelLength`**: it is the length
 * of the *selected* channel, not of the document, and §5 guarantees those differ.
 * A strip that showed "the" length would be asserting channels are equal, which
 * is the one thing this app never assumes.
 */

import type { SoundDocument } from "../document.js";
import { fixedToDouble } from "../units.js";
import { frameSizeFor } from "../samples.js";

/** Preferences page 2's five checkboxes. `[RES: DITL 1002]` */
export interface InfoStripPrefs {
  showRateAndSize: boolean;
  showChannels: boolean;
  showFileSize: boolean;
  showLengthSamples: boolean;
  showLengthSeconds: boolean;
  /** "Use custom background color" — page 2's colour well. */
  customBackground: string | null;
}

export const defaultInfoStripPrefs: InfoStripPrefs = {
  showRateAndSize: true,
  showChannels: true,
  showFileSize: true,
  showLengthSamples: true,
  showLengthSeconds: true,
  customBackground: null,
};

/**
 * "22.050 kHz" — three decimals, which is what §3's example shows and what the
 * Mac's own rates need: 22254.5454 Hz prints as "22.255 kHz" and the odd one out,
 * 22.254, is why the Sampling Rate popup (MENU 155) spells it "22.254 kHz –
 * Standard" rather than rounding.
 */
export function formatRate(rateFixed: number): string {
  return `${(fixedToDouble(rateFixed) / 1000).toFixed(3)} kHz`;
}

/** §3's example: "22.050 kHz; 16 bits". Singular for 1 bit — the model allows it. */
export function formatRateAndSize(doc: SoundDocument): string {
  return `${formatRate(doc.rate)}; ${doc.bps} ${doc.bps === 1 ? "bit" : "bits"}`;
}

/** §3: "Mono"/"Stereo"/"N channels". The app's own words for 1, 2 and more. */
export function formatChannels(numChans: number): string {
  if (numChans === 1) return "Mono";
  if (numChans === 2) return "Stereo";
  return `${numChans} channels`;
}

/**
 * The document's size in bytes — the sum over channels.
 *
 * A sum, not `maxChSize × numChans`: with unequal channels those disagree, and
 * the honest number is what the buffers actually hold.
 */
export function documentSizeBytes(doc: SoundDocument): number {
  let total = 0;
  for (const ch of doc.channels) total += ch.data.length;
  return total;
}

export function formatFileSize(bytes: number): string {
  return `${bytes.toLocaleString("en-US")} bytes`;
}

/** The **selected** channel — `firstSelCh` is 1-based (§2.4). */
export function selectedChannelLengthBytes(doc: SoundDocument): number {
  return doc.channels[doc.firstSelCh - 1]?.data.length ?? 0;
}

export function formatLengthSamples(doc: SoundDocument): string {
  const samples = Math.floor(selectedChannelLengthBytes(doc) / frameSizeFor(doc.bps));
  return `${samples.toLocaleString("en-US")} samples`;
}

export function formatLengthSeconds(doc: SoundDocument): string {
  const samples = Math.floor(selectedChannelLengthBytes(doc) / frameSizeFor(doc.bps));
  const hz = fixedToDouble(doc.rate);
  return `${(hz === 0 ? 0 : samples / hz).toFixed(3)} s`;
}

/** The strip's visible fields, in §3's order, honouring the page-2 toggles. */
export function infoStripFields(doc: SoundDocument, prefs: InfoStripPrefs): string[] {
  const out: string[] = [];
  if (prefs.showRateAndSize) out.push(formatRateAndSize(doc));
  if (prefs.showChannels) out.push(formatChannels(doc.numChans));
  if (prefs.showFileSize) out.push(formatFileSize(documentSizeBytes(doc)));
  if (prefs.showLengthSamples) out.push(formatLengthSamples(doc));
  if (prefs.showLengthSeconds) out.push(formatLengthSeconds(doc));
  return out;
}
