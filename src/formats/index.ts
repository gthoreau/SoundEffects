/**
 * File formats — SPEC §10, build-order step 6.
 *
 * ## Scope
 *
 * **AIFF and WAV only.** §10's Open list also has System 7 sound files (`sfil` —
 * an `snd ` resource living in a *resource fork*) and Sound Designer II. Both are
 * dropped deliberately: they are HFS resource-fork formats that no modern
 * filesystem carries — the repo's own `.gitignore` says as much about the extracted
 * app folder — so they would be museum pieces with no files to read. §10 already
 * names AIFF the canonical interchange and suggests WAV "for convenience"; this is
 * that advice, taken.
 *
 * ## What modern specs we accept, and why the two answers differ
 *
 * **Bit depth: everything, and it costs nothing.** The model is `bps 1..32` packed
 * big-endian (§2.2), so 24- and 32-bit integer are native. The 1994 model is *more*
 * general here than WAV usually needs. The single exclusion is **float**, and it is
 * an identity constraint rather than a gap: every effect in this app is integer
 * (`s * gain / 100`, truncating), and §2.2 calls that rounding "part of the sound".
 * Float files convert to 32-bit integer at the door.
 *
 * **Sample rate: everything, since the rate field was widened.** This was once the
 * hard half of the answer: `doc.rate` is a `Fixed` `[SOURCE: Glue.h]`, and in its
 * original *unsigned 32-bit* container it topped out at 65535.99998 Hz, so 88.2,
 * 96, 176.4 and 192 kHz could not be represented at all — they had to be refused
 * or downsampled at the door. `units.ts` now keeps the 16 fractional bits (and
 * their characteristic quantisation) while lifting the 16-bit integer cap, so
 * modern rates import natively and exactly. The argument for that divergence, and
 * its cost, are set out in `units.ts`.
 *
 * `resampleTo` survives as a *user preference* — "import this 96 kHz file at
 * 48 kHz" — rather than a rescue. When used, it goes through the app's own
 * `ModResampleSoundRate`: drop-sample, truncating, no interpolation. Slipping in a
 * nicer resampler would make imports sound unlike every other rate change in the
 * program, so the aliasing is the honest answer.
 */

import type { SoundDocument } from "../document.js";
import { modResampleSoundRate } from "../runtime/resample.js";
import { doubleToFixed } from "../units.js";
import { MAX_FIXED_RATE_HZ } from "../effects/keyboard.js";
import { isAiff, decodeAiff, writeAiff, parseAiff } from "./aiff.js";
import { isWav, decodeWav, writeWav, parseWav, WaveFormat, wavRateLoss } from "./wav.js";
import { documentFromChannels, exportWouldPad } from "./pcm.js";

export * from "./extended80.js";
export * from "./pcm.js";
export * from "./aiff.js";
export * from "./wav.js";

export type SoundFormat = "aiff" | "wav";

/**
 * "I can only open System 7 sound files or AIFF files… You may want to use
 * SoundExtractor to convert…" `[RES]`
 *
 * §10 says to keep this alert for unknown types, "reworded only if unavoidable".
 * It is unavoidable: we no longer open System 7 files, so promising to would be a
 * lie. The first person, the apology and the pointer-to-another-tool are the parts
 * that matter, and they survive.
 */
export const UNKNOWN_FORMAT_ALERT =
  "I can only open AIFF or WAV files. You may want to convert this one first.";

/** The rate ceiling, restated where importers will look for it. */
export const MAX_RATE_HZ = MAX_FIXED_RATE_HZ;

/** Sniff the format from the file's magic. */
export function detectFormat(bytes: Uint8Array): SoundFormat | null {
  if (isAiff(bytes)) return "aiff";
  if (isWav(bytes)) return "wav";
  return null;
}

/** What an import would have to reconcile before the document can exist. */
export interface ImportIssues {
  /**
   * The file's rate, when it exceeds what the rate field holds exactly.
   *
   * Since the widening this means ~137 GHz, so it never fires for real audio —
   * kept because the limit is real rather than imaginary.
   */
  rateTooHigh?: { fileRate: number; maxRate: number };
  /** The file is float; it will become integer. */
  floatToInteger?: { fileBits: number; targetBps: number };
  /** AIFC with a compression type we have no decoder for. */
  compressed?: { type: string };
}

/**
 * Inspect a file before committing to it — the "ask first" half.
 *
 * Returns null when the file drops straight in.
 */
export function inspectImport(bytes: Uint8Array): ImportIssues | null {
  const format = detectFormat(bytes);
  if (!format) throw new Error(UNKNOWN_FORMAT_ALERT);
  const issues: ImportIssues = {};

  if (format === "aiff") {
    const info = parseAiff(bytes);
    if (info.compressed) issues.compressed = { type: info.compressionType ?? "????" };
    if (info.rateHz > MAX_RATE_HZ) {
      issues.rateTooHigh = { fileRate: info.rateHz, maxRate: MAX_RATE_HZ };
    }
  } else {
    const info = parseWav(bytes);
    if (info.formatTag === WaveFormat.ieeeFloat) {
      issues.floatToInteger = { fileBits: info.bitsPerSample, targetBps: 32 };
    }
    if (info.sampleRate > MAX_RATE_HZ) {
      issues.rateTooHigh = { fileRate: info.sampleRate, maxRate: MAX_RATE_HZ };
    }
  }

  return Object.keys(issues).length > 0 ? issues : null;
}

/**
 * The resample prompt for a too-high rate, in the app's own voice.
 *
 * Modelled on ALRT 152/166's paste prompt — "You are trying to paste a sound
 * sampled at ^1 bits into a sound sampled at ^2 bits. Do you want me to resample
 * the sound you are inserting at ^3 bits?" — because it is the same question.
 */
export function rateTooHighAlert(fileRate: number, targetRate: number): string {
  return (
    `This sound is sampled at ${Math.round(fileRate)} Hz, and I can't handle anything ` +
    `above ${Math.floor(MAX_RATE_HZ)} Hz. Do you want me to resample it at ` +
    `${Math.round(targetRate)} Hz?`
  );
}

export interface ImportOptions {
  name?: string;
  /**
   * Resample the file to this rate on the way in.
   *
   * A **preference**, not a rescue: every real rate now imports natively, so this
   * is for "I want this 96 kHz file at 48". It goes through the app's own
   * drop-sample resampler, aliasing included.
   */
  resampleTo?: number;
}

/**
 * Read a file into a document.
 *
 * Every rate people actually use — up to and including 192 kHz — imports natively
 * and exactly, since the rate field was widened. `resampleTo` downsamples on the
 * way in if you want that, through the app's **own** resampler: drop-sample,
 * truncating, no interpolation (`Host-Resamplers.md`), so an import aliases
 * exactly as every other rate change in this program aliases.
 */
export function readSound(bytes: Uint8Array, opts: ImportOptions = {}): SoundDocument {
  const format = detectFormat(bytes);
  if (!format) throw new Error(UNKNOWN_FORMAT_ALERT);
  const name = opts.name ?? "untitled sound";

  const decoded = format === "aiff" ? decodeAiff(bytes) : decodeWav(bytes);
  const doc = documentFromChannels(decoded, name);

  if (opts.resampleTo !== undefined && opts.resampleTo !== decoded.rateHz) {
    if (opts.resampleTo > MAX_RATE_HZ) {
      throw new Error(`${opts.resampleTo} Hz is above the rate field's ceiling`);
    }
    modResampleSoundRate(doc, doubleToFixed(opts.resampleTo));
  }
  return doc;
}

/** What an export would lose. */
export interface ExportIssues {
  /**
   * Channels have drifted apart and the format has one length: they will be
   * padded to the longest with silence, and re-opening will not give the drift
   * back.
   */
  padsChannels?: { lengths: number[] };
  /** WAV's integer rate field cannot hold this document's fractional rate. */
  rateFractionLost?: { exact: number; stored: number };
}

/**
 * Inspect an export before writing.
 *
 * The padding one matters more than it looks. **Unequal channel lengths are the
 * app's core weirdness** (§5: "since channels are independent buffers, this
 * shortens *only those channels*, letting tracks drift out of alignment") — and
 * neither AIFF nor WAV can express them. A document whose tracks have drifted
 * comes back from disk aligned. The scars of a one-channel Clear do not survive a
 * round trip, and the user deserves to know that before the file is written.
 */
export function inspectExport(doc: SoundDocument, format: SoundFormat): ExportIssues | null {
  const issues: ExportIssues = {};
  if (exportWouldPad(doc)) {
    issues.padsChannels = { lengths: doc.channels.map((c) => c.data.length) };
  }
  if (format === "wav" && wavRateLoss(doc) > 0) {
    const exact = doc.rate / 65536;
    issues.rateFractionLost = { exact, stored: Math.round(exact) };
  }
  return Object.keys(issues).length > 0 ? issues : null;
}

/** "Your channels are different lengths…" — worth saying in the app's voice. */
export const PADS_CHANNELS_ALERT =
  "Your channels aren't all the same length, and this format can't remember that. " +
  "I'll pad the short ones with silence. Opening the file again won't bring the difference back.";

/** Write a document to a file. */
export function writeSound(doc: SoundDocument, format: SoundFormat): Uint8Array {
  return format === "aiff" ? writeAiff(doc) : writeWav(doc);
}
