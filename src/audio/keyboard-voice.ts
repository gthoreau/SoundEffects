/**
 * Keyboard's auditioning half — SPEC §8 #18, §6.
 *
 * The other half (`effects/keyboard.ts`) is the Change Pitch commit, which is
 * pure header edit and needs no audio. This is the part that needed step 5: you
 * click a key, and it plays.
 *
 * There is almost nothing here, and that *is* the module. A chromatic sampler
 * built from a rate multiplier needs no voice allocator, no envelope, no
 * interpolation — it needs `Transport.play({ rateScale })`. Clicking a key is
 * playing the sound wrong on purpose.
 */

import type { SoundDocument } from "../document.js";
import {
  keyboardChannel,
  needsOneChannelAlert,
  pitchScale,
  semitonesFromBase,
  KEYBOARD_ONE_CHANNEL_ONLY,
} from "../effects/keyboard.js";
import type { Transport } from "./transport.js";

export interface AuditionResult {
  /** The channel that sounded, 0-based. */
  channel: number;
  /** The rate multiplier applied. */
  rateScale: number;
  /** The apologetic alert, when more than one channel was selected. */
  alert: string | null;
}

/**
 * Play `midiNote` on the keyboard.
 *
 * Plays the **first selected channel only** and reports the alert when it had to
 * choose — the host shows it. Auditioning never touches the document, including
 * its rate: the scale is passed to the transport for this note only. Only Change
 * Pitch commits.
 *
 * `useSelectionForLooping` maps onto the transport's existing loop path, so a
 * held key sustains by looping the selection — which is how you turn a one-shot
 * into a playable instrument, and the reason that checkbox exists.
 */
export function auditionNote(
  doc: SoundDocument,
  transport: Transport,
  midiNote: number,
  useSelectionForLooping = false,
): AuditionResult | null {
  const channel = keyboardChannel(doc);
  if (channel === null) return null;

  const semitones = semitonesFromBase(doc, midiNote);
  const rateScale = pitchScale(semitones);

  transport.play({
    rateScale,
    soloChannel: channel,
    loop: useSelectionForLooping,
  });

  return {
    channel,
    rateScale,
    alert: needsOneChannelAlert(doc) ? KEYBOARD_ONE_CHANNEL_ONLY : null,
  };
}

/** Release a key. */
export function releaseNote(transport: Transport): void {
  transport.stop();
}
