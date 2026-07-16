/**
 * The front door — File > Open and File > Save As (§10).
 *
 * Step 6 built and tested the whole of AIFF and WAV; none of that is repeated
 * here. This module is the browser's file picker plus the *alerts*, because the
 * alerts are where §10's policy lives: what we refuse, what we warn about, and
 * whose voice it comes out in.
 *
 * The one that matters is {@link confirmExport}. §5's unequal channel lengths
 * are the app's core weirdness, and **neither format can express them** — a
 * document whose tracks have drifted comes back from disk aligned. Step 6 wrote
 * `inspectExport` so the user could be told *before* the file is written rather
 * than discovering it on re-open. Wiring that warning up is most of the reason
 * this file exists.
 */

import type { SoundDocument } from "../document.js";
import {
  PADS_CHANNELS_ALERT,
  type SoundFormat,
  UNKNOWN_FORMAT_ALERT,
  inspectExport,
  inspectImport,
  rateTooHighAlert,
  readSound,
  writeSound,
} from "../formats/index.js";
import { showAlert } from "./alert.js";

/** Strip the extension: "Beep.aiff" opens as the document "Beep". */
function documentName(filename: string): string {
  return filename.replace(/\.(aiff?|aifc|wav|wave)$/i, "");
}

/** Prompt for a file. Resolves to null if the user cancels. */
export function pickFile(): Promise<File | null> {
  return new Promise((resolve) => {
    const input = document.createElement("input");
    input.type = "file";
    input.accept = ".aif,.aiff,.aifc,.wav,.wave,audio/aiff,audio/wav,audio/x-wav";
    input.style.display = "none";
    document.body.append(input);

    // There is no reliable "cancelled" event across browsers. `change` fires on
    // pick; if it never fires, the picker was dismissed and the input is simply
    // collected — a leaked hidden input is a better outcome than a hung promise.
    input.addEventListener("change", () => {
      const file = input.files?.[0] ?? null;
      input.remove();
      resolve(file);
    });
    input.click();
  });
}

/**
 * Open a file into a document, asking about anything §10 says to ask about.
 *
 * Returns null when the user cancels or the file is refused — the caller opens
 * no window in that case. Every alert here is a real resource string or the
 * reworded one §10 explicitly licenses.
 */
export async function openSoundFile(file: File): Promise<SoundDocument | null> {
  const bytes = new Uint8Array(await file.arrayBuffer());

  let issues;
  try {
    issues = inspectImport(bytes);
  } catch {
    // detectFormat found no magic. §10: keep the alert for unknown types.
    await showAlert({ message: UNKNOWN_FORMAT_ALERT });
    return null;
  }

  if (issues?.compressed) {
    await showAlert({
      message: `I can't read this AIFF: it's compressed (${issues.compressed.type}), and I only understand uncompressed sound.`,
    });
    return null;
  }

  // Since the rate field was widened (§14 item 27) this cannot fire for real
  // audio — 192 kHz is nowhere near the ~137 GHz ceiling. Kept because the limit
  // is real, and because refusing silently would be worse than a dead branch.
  let resampleTo: number | undefined;
  if (issues?.rateTooHigh) {
    const answer = await showAlert({
      message: rateTooHighAlert(issues.rateTooHigh.fileRate, issues.rateTooHigh.maxRate),
      icon: "caution",
      buttons: [
        { text: "Resample", isDefault: true },
        { text: "Cancel", isCancel: true },
      ],
    });
    if (answer !== 0) return null;
    resampleTo = issues.rateTooHigh.maxRate;
  }

  // Float → integer is not a question. §2.2 makes integer truncation "part of
  // the sound", so there is no float document to offer; step 6 converts at the
  // door and the user is told, not asked.
  if (issues?.floatToInteger) {
    await showAlert({
      message:
        `This is a ${issues.floatToInteger.fileBits}-bit floating-point sound, and I work in whole numbers. ` +
        `I'll open it as ${issues.floatToInteger.targetBps}-bit.`,
      icon: "note",
      buttons: [{ text: "OK", isDefault: true }],
    });
  }

  try {
    return readSound(bytes, { name: documentName(file.name), resampleTo });
  } catch (err) {
    await showAlert({
      message: err instanceof Error ? err.message : UNKNOWN_FORMAT_ALERT,
    });
    return null;
  }
}

/**
 * Warn about anything the export will lose, and let the user back out.
 *
 * Resolves false if they cancel.
 */
export async function confirmExport(doc: SoundDocument, format: SoundFormat): Promise<boolean> {
  const issues = inspectExport(doc, format);
  if (!issues) return true;

  if (issues.padsChannels) {
    const answer = await showAlert({
      message: PADS_CHANNELS_ALERT,
      icon: "caution",
      buttons: [
        { text: "Save", isDefault: true },
        { text: "Cancel", isCancel: true },
      ],
    });
    if (answer !== 0) return false;
  }

  // §14 item 29: WAV's integer rate field rounds 22254.5454 to 22255, so the
  // Mac's own native rate cannot round-trip through WAV. AIFF carries the same
  // 80-bit extended the app computed in and comes back exact — which is why §10
  // calls AIFF canonical, and why this is worth surfacing at the save dialog
  // rather than leaving as a footnote.
  if (issues.rateFractionLost) {
    const answer = await showAlert({
      message:
        `This sound is sampled at ${issues.rateFractionLost.exact.toFixed(4)} Hz, and WAV can only ` +
        `remember whole numbers — it'll come back as ${issues.rateFractionLost.stored} Hz. ` +
        `Save it as AIFF instead and I can give it back to you exactly.`,
      icon: "caution",
      buttons: [
        { text: "Save as WAV", isDefault: true },
        { text: "Cancel", isCancel: true },
      ],
    });
    if (answer !== 0) return false;
  }
  return true;
}

/** Hand the bytes to the browser's downloader. */
export function downloadSound(doc: SoundDocument, format: SoundFormat): void {
  const bytes = writeSound(doc, format);
  // Copy into a fresh buffer: the document's channel buffers are views onto
  // memory the editor keeps mutating, and Blob does not promise to snapshot.
  const blob = new Blob([new Uint8Array(bytes)], {
    type: format === "aiff" ? "audio/aiff" : "audio/wav",
  });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `${doc.name}.${format === "aiff" ? "aiff" : "wav"}`;
  a.click();
  URL.revokeObjectURL(url);
}
