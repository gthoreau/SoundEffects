/**
 * The app's voice — SPEC §11, `[RES: STR# 128]`.
 *
 * §1 makes the first person a *contract*, not a style: this program says "I
 * couldn't", apologises, and blames itself. So the strings are never rewritten
 * here — they are looked up out of the fork verbatim and only have their
 * placeholders filled.
 *
 * Two conventions the resources use and this module preserves:
 *
 * - **`∑` is the parameter placeholder** (not `%s`, not `^0`). 39 of the 64
 *   strings in STR# 128 carry at least one, and where a string has several they
 *   fill **positionally, in order** — "I couldn't save “∑” because the disk “∑”
 *   is full" takes the document first, the disk second.
 * - **`\r` is the line break** (classic Mac line ending), not `\n`.
 *
 * The typographic characters are deliberate too: the corpus uses curly quotes
 * (“…”) and a real apostrophe (’). Straightening them would be a rewrite.
 */

import { Res, getIndString, STR_LIST } from "./resources.js";

/** The placeholder character the resources actually use. */
export const PLACEHOLDER = "∑";

/**
 * Fill a resource string's `∑` placeholders positionally.
 *
 * Extra arguments are ignored and missing ones leave the `∑` standing — the
 * original would have shown a bare ∑ too, and a visible one in the UI is a
 * better bug report than a silent "undefined".
 */
export function fill(template: string, ...args: (string | number)[]): string {
  let i = 0;
  return template.replace(/∑/g, () => (i < args.length ? String(args[i++]) : PLACEHOLDER));
}

/**
 * A string from the 64-string error corpus, placeholders filled.
 *
 * (§11 says 66; the fork holds **64**. See the step-7 note in §15.)
 */
export function errorString(index: number, ...args: (string | number)[]): string {
  return fill(getIndString(Res.errors, index), ...args);
}

/** Split a resource string into display lines on the classic `\r`. */
export function lines(s: string): string[] {
  return s.split("\r");
}

/**
 * Every string in the corpus, 1-based, for tests and for the About box.
 * Reaching for this at runtime means you want {@link errorString} instead.
 */
export function errorCorpus(): readonly string[] {
  return STR_LIST[String(Res.errors)] ?? [];
}

/**
 * Named indices into STR# 128 for the errors the shell itself raises.
 *
 * Only the ones this port actually reaches are named — the corpus is full of
 * strings about AppleEvents, SCSI disks and Undo temp files that no browser will
 * ever produce. They stay in the JSON regardless; the fidelity is in *not*
 * pruning the corpus, but naming an index we never raise would be pretending.
 */
export const Err = {
  unknown: 1,
  notEnoughMemory: 2,
  noSoundsInDocument: 3,
  couldntOpenDocument: 4,
  /** "I'm sorry, but I can't delete the only channel of a mono sound…" (§3) */
  cantDeleteOnlyChannel: 24,
  cantShiftEmptySound: 31,
} as const;

/**
 * The Cut/Copy/Paste/Clear ⇄ …Channel relabelling of §4. `[RES: STR# 136]`
 *
 * STR# 136 stores the two sets adjacently — plain at 1–4, the Channel variants
 * at 5–8 — so the whole feature is `index + (whole ? 4 : 0)`. That layout *is*
 * the implementation; the menu is relabelled, not rebuilt.
 */
export const EditVerb = { cut: 1, copy: 2, paste: 3, clear: 4 } as const;
export type EditVerbCode = (typeof EditVerb)[keyof typeof EditVerb];

export function editMenuLabel(verb: EditVerbCode, wholeChannels: boolean): string {
  return getIndString(Res.editLabels, verb + (wholeChannels ? 4 : 0));
}
