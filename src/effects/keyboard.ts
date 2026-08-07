/**
 * **Keyboard** — SPEC §8 #18. `[DOC+RES]`
 *
 * A sampler built out of nothing but a rate multiplier, and the app's whole
 * personality in one button.
 *
 * You click a key; it plays the first selected channel at that chromatic pitch by
 * scaling the playback rate. Then **Change Pitch** commits — by editing the
 * document's `rate` field and touching **not one byte of audio**. The header now
 * lies about the sound. If you want the lie to become true you go and run Resample
 * Rate yourself; the app will not do it for you, and it will not warn you that it
 * hasn't. It even only plays the first selected channel, with an apologetic alert
 * about it.
 *
 * ## Why this is in `effects/` and yet needed step 5
 *
 * It is a real module in the Effects Folder (§7.1, top level) and it takes a menu
 * item like any other, so it belongs here. But it is not a buffer effect: it has
 * no `effect()` kernel that chews on samples. Its two halves are *auditioning*
 * (playback-rate scaling — step 5's transport) and *committing* (a header edit).
 * The commit was always buildable headless. The audition is what arrived with the
 * transport, which is why Keyboard lands now rather than with the UI shell.
 *
 * What is still missing is only the window with keys in it (step 7).
 */

import type { SoundDocument } from "../document.js";
import { doubleToFixed, fixedToDouble, MAX_FIXED_RATE_HZ, type Fixed } from "../units.js";

/** Re-exported: it used to live here, and callers still look for it. */
export { MAX_FIXED_RATE_HZ };
import { kModCancel, kModNoError, type ModResult } from "../runtime/results.js";
import { ItemType, type SettingsSchema } from "../runtime/schema.js";
import type { EffectModule } from "../runtime/module.js";

/**
 * STR# 1000, verbatim. `[RES]`
 *
 * These were placeholders in the app's own voice until the shipping module's
 * resource fork was read: `SoundEffects 0.9.2 ƒ/Effects Folder/Keyboard` still
 * carries STR# 1000, and these are its strings, index for index — typo, curly
 * apostrophes and all. "Keboard" is the original's own slip and stays.
 *
 * `KEYBOARD_ONE_CHANNEL_ONLY` is the apologetic alert §8 #18 records: string #3,
 * raised when the keyboard window opens on more than one selected channel.
 */
export const KEYBOARD_NO_SETTINGS = "The Keyboard module does not have any settings.";
export const KEYBOARD_ONE_CHANNEL_ONLY =
  "I’m sorry, but with this version of the Keboard module you will be able to play " +
  "the first selected channel only.\n" +
  "You might want to mix the sound to a single channel next time.";
export const KEYBOARD_NO_CHANNEL =
  "I couldn’t open a sound channel to work with. Either there is not enough memory, " +
  "or there are too many sounds already playing.";
export const KEYBOARD_EMPTY_SOUND =
  "You are using the Keyboard module with an empty sound. You won’t hear anything.";

/** The MIDI note the document's `baseFreq` names. Default 60 = middle C (§2.1). */
export const DEFAULT_BASE_FREQ = 60;

export interface KeyboardSettings {
  /**
   * Semitones from the document's `baseFreq`. What the last-clicked key set.
   * Change Pitch commits *this*.
   */
  semitones: number;
  /** ☐ Use selection for looping. */
  useSelectionForLooping: boolean;
}

/**
 * The rate multiplier for a chromatic offset: `2^(semitones/12)`.
 *
 * This is the entire synthesis engine. Twelve-tone equal temperament falls out of
 * a single `Math.pow`, and the "sampler" is a `*=`.
 */
export function pitchScale(semitones: number): number {
  return Math.pow(2, semitones / 12);
}

/** Semitones between a MIDI note and the document's base note. */
export function semitonesFromBase(doc: SoundDocument, midiNote: number): number {
  return midiNote - (doc.baseFreq || DEFAULT_BASE_FREQ);
}

/**
 * The rate a key *auditions* at — the document's declared rate, scaled.
 *
 * Returned as Fixed, because that is what the transport reads and what Change
 * Pitch will write. Rounding here is not incidental: see `changePitch`.
 */
export function auditionRate(doc: SoundDocument, semitones: number): Fixed {
  return doubleToFixed(fixedToDouble(doc.rate) * pitchScale(semitones));
}

/**
 * **Change Pitch** — the commit. Edits `doc.rate` and nothing else.
 *
 * Not a single sample is touched. The document now claims a rate it was never
 * recorded at, every duration in the app silently re-reads (a selection's length
 * in seconds changes without a byte moving, because §2.3 derives seconds from
 * `samples / rate`), and playback obeys the lie because the transport trusts the
 * header. Pair with Resample Rate to make it real.
 *
 * **The Fixed rounding is lossy**, and that survives the widening in `units.ts`:
 * each commit still quantises to 1/65536 Hz. At some rates twelve successive
 * +1-semitone commits land a hair off a single +12 (8000 Hz drifts by ~46 µHz);
 * at others they agree exactly, because doubling an already-quantised value is
 * itself exact. Do not assume either — both are pinned by tests.
 *
 * What no longer happens is the **32-bit wrap**: pitching a 44.1 kHz sample up an
 * octave used to ask for 88.2 kHz, overflow the container, and land on 22664 Hz —
 * you pressed "up" and it went *down*. The integer part is no longer capped at 16
 * bits, so it now simply lands on 88.2 kHz. That quirk was never a recovered
 * behaviour (§14 item 23: the original never met a rate it couldn't hold, so it
 * never had to decide), and losing it costs no fidelity — see the argument in
 * `units.ts`.
 *
 * @returns the rate actually stored, after Fixed quantisation.
 */
export function changePitch(doc: SoundDocument, semitones: number): Fixed {
  doc.rate = auditionRate(doc, semitones);
  return doc.rate;
}

/**
 * Would committing this pitch exceed what the rate field holds exactly?
 *
 * Retained for honesty — the ceiling is now ~137 GHz rather than 65.5 kHz, so in
 * practice this never fires for audio. Kept because the limit is real rather than
 * imaginary, and a host that wants to be careful can still ask.
 */
export function changePitchOverflows(doc: SoundDocument, semitones: number): boolean {
  return fixedToDouble(doc.rate) * pitchScale(semitones) > MAX_FIXED_RATE_HZ;
}

/** The 0-based index of the channel Keyboard will play, or null if none. */
export function keyboardChannel(doc: SoundDocument): number | null {
  const idx = doc.firstSelCh - 1;
  return doc.channels[idx] ? idx : null;
}

/** Does this document need the apologetic alert? More than one channel selected. */
export function needsOneChannelAlert(doc: SoundDocument): boolean {
  return doc.lastSelCh > doc.firstSelCh;
}

/**
 * The Keyboard module.
 *
 * `effect()` is the Change Pitch commit: in the original the menu item opens the
 * window and Change Pitch is a button inside it, so the window is the settings
 * dialog (step 7) and this is what OK does. Auditioning is
 * `Transport.play({ rateScale })` — see `audio/keyboard-voice.ts`.
 */
export const Keyboard: EffectModule<KeyboardSettings> = {
  name: "Keyboard",
  signature: "Keyboard",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "",
  // TEXT 1000, verbatim. `[RES]` — recovered alongside STR# 1000, replacing a
  // paraphrase written before the module's resource fork was read.
  about:
    "The Keyboard module lets you play the selected sound at various pitches, depending on " +
    "where you click on a piano keyboard.\n" +
    "If more than one channel is selected when you use this module, only the first selected " +
    "channel will play.\n\n" +
    "If you click the “Change Pitch” button, the pitch of the sound will be changed to the " +
    "one that corresponds to the last clicked piano key. To change the pitch of the sound, " +
    "however, the module does not change the sound data itself. Instead, it modifies the " +
    "playback rate.\n" +
    "So, if you don’t want the playback rate to be changed, you will have to do a “Resample " +
    "Rate” effect on it, specifying the original rate.\n\n" +
    "Thanks to Hoby Van Hoose.",
  // Indexed by |result| (§7.7), so the empty slot at #2 is load-bearing.
  strings: [
    KEYBOARD_NO_SETTINGS,
    "",
    KEYBOARD_ONE_CHANNEL_ONLY,
    KEYBOARD_NO_CHANNEL,
    KEYBOARD_EMPTY_SOUND,
  ],

  defaults: () => ({ semitones: 0, useSelectionForLooping: false }),

  schema: (): SettingsSchema => [
    { type: ItemType.editFieldNumerical, key: "semitones", label: "Semitones:" },
    { type: ItemType.checkBox, key: "useSelectionForLooping", label: "Use selection for looping" },
  ],

  effect(doc, _host, prefs): ModResult {
    if (keyboardChannel(doc) === null) return kModCancel;
    // Unison is a no-op: there is no pitch to change.
    if (prefs.semitones === 0) return kModCancel;
    changePitch(doc, prefs.semitones);
    // kModNoError means "success, data modified" (§7.7) — and it is *true* here in
    // the only sense the ABI cares about: the document changed. That not a byte of
    // audio moved is precisely the joke.
    return kModNoError;
  },
};
