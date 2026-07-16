/**
 * The output stage — SPEC §6, §2.1, §3.
 *
 * Packed buffers become Float32 **only here, at the output edge**, exactly as
 * §12 prescribes. Everything upstream stays in the original packed big-endian
 * format with its integer scars intact; this file is the one place that is
 * allowed to think in floats, and it does no DSP beyond summing and scaling.
 *
 * The routing is the interesting part, and it is pure fidelity: the two speaker
 * toggles beside each lane in the wave editor (§3) are the same toggles that
 * route the mix, which is why what you hear while editing is what Quick Mix
 * gives you when you commit.
 */

import type { SoundDocument } from "../document.js";
import { frameSizeFor, getSample } from "../samples.js";

/**
 * The 8-step volume popup: Off, 1–7. `[RES: MENU / CNTL]`
 *
 * `[INTERPRETED]`: the *steps* are recovered (§3's transport, §6, the Sound menu),
 * the *curve* is not. Linear n/7 is the reading — the Sound Manager's own volume
 * was a plain 8.8 scalar with 0x0100 = unity, so a linear ladder to unity is what
 * the hardware made cheap and what 1994 would have shipped. If a taper is ever
 * recovered, this table is the one place to change.
 */
export const VOLUME_STEPS = [0, 1 / 7, 2 / 7, 3 / 7, 4 / 7, 5 / 7, 6 / 7, 1] as const;

/** Volume ▸ Off. */
export const VOLUME_OFF = 0;
/** Volume ▸ 7 — full. */
export const VOLUME_FULL = 7;

export function volumeScalar(step: number): number {
  return VOLUME_STEPS[Math.max(0, Math.min(7, Math.trunc(step)))] ?? 1;
}

/**
 * Per-channel monitoring state, driven by the function keys (§4's keyboard map):
 * `F1–Fn` = solo-play channel n, `option-Fn` = mute channel n.
 */
export interface ChannelMonitor {
  /** Muted channels are silent. option-Fn. */
  muted: Set<number>;
  /**
   * If non-empty, ONLY these channels sound. F1–Fn.
   * Solo is a filter, not a per-channel flag, because that is how the key works:
   * pressing F2 plays channel 2 and nothing else.
   */
  soloed: Set<number>;
}

export function newMonitor(): ChannelMonitor {
  return { muted: new Set(), soloed: new Set() };
}

/** Is this 0-based channel audible right now? */
export function isAudible(monitor: ChannelMonitor, channel: number): boolean {
  if (monitor.muted.has(channel)) return false;
  if (monitor.soloed.size > 0) return monitor.soloed.has(channel);
  return true;
}

/**
 * The float divisor for a bit depth: 2^(bits−1).
 *
 * Note this is the same off-by-one convention Dither uses for its percentage mode
 * (`2^(bps−1)` is 128 at 8-bit, one more than the true max sample of 127). Here
 * it is not an off-by-one at all but the correct choice: it maps −128 to exactly
 * −1.0 and 127 to 0.992, so full-scale negative doesn't overflow the float range.
 */
export function floatDivisor(bps: number): number {
  const bits = Math.min(frameSizeFor(bps), 4) * 8;
  return Math.pow(2, bits - 1);
}

export interface MixOptions {
  /** 0–7. */
  volume: number;
  monitor: ChannelMonitor;
  /** A backend that mixes stereo down to mono, as `mixesStereoOutputToMono` did. */
  forceMono?: boolean;
}

/**
 * Mix one frame of the document at byte offset `pos` into `[left, right]`.
 *
 * Fidelity points, all of them observable:
 *
 *  - **A channel shorter than `pos` contributes nothing and the rest play on.**
 *    Unequal channels simply end. This is the single most characteristic thing
 *    about playing a SoundEffects document, and it falls straight out of the
 *    model rather than needing special handling.
 *  - Each channel is scaled by its own 8.8 `volume` (0x0100 = 1.0), and **values
 *    above unity overdrive on purpose** (§2.1) — no normalisation, no protection.
 *  - Routing is by the lane's own L/R speaker enables. A channel with both off is
 *    inaudible even though it is neither muted nor un-soloed.
 *  - The sum is **not** scaled by channel count. Two loud channels clip, exactly
 *    as Quick Mix's mixdown clips. The app does not protect you from yourself.
 */
export function mixFrame(
  doc: SoundDocument,
  pos: number,
  opts: MixOptions,
): [number, number] {
  const frameSize = frameSizeFor(doc.bps);
  const divisor = floatDivisor(doc.bps);
  const master = volumeScalar(opts.volume);
  let left = 0;
  let right = 0;

  for (let c = 0; c < doc.numChans; c++) {
    if (!isAudible(opts.monitor, c)) continue;
    const ch = doc.channels[c]!;
    // Past this channel's end? It simply contributes nothing.
    if (pos + frameSize > ch.data.length) continue;

    // 8.8 fixed: 0x0100 = 1.0, >1.0 overdrives, <=0 is silent.
    const chVol = ch.volume / 0x0100;
    if (chVol <= 0) continue;

    const s = (getSample(ch.data, pos, doc.bps) / divisor) * chVol;
    if (ch.outLeft) left += s;
    if (ch.outRight) right += s;
  }

  left *= master;
  right *= master;

  if (opts.forceMono) {
    const m = left + right;
    left = m;
    right = m;
  }

  // Clamp at the very edge, after everything. A float that leaves here out of
  // range is a click on real hardware.
  return [clampFloat(left), clampFloat(right)];
}

function clampFloat(v: number): number {
  if (v < -1) return -1;
  if (v > 1) return 1;
  return v;
}

/**
 * Render a whole byte range of the document to planar floats, ignoring rate
 * conversion. Used by the transport and by anything wanting an offline bounce.
 */
export function mixRange(
  doc: SoundDocument,
  start: number,
  end: number,
  opts: MixOptions,
): [Float32Array, Float32Array] {
  const frameSize = frameSizeFor(doc.bps);
  const frames = Math.max(0, Math.floor((end - start) / frameSize));
  const left = new Float32Array(frames);
  const right = new Float32Array(frames);
  for (let i = 0; i < frames; i++) {
    const [l, r] = mixFrame(doc, start + i * frameSize, opts);
    left[i] = l;
    right[i] = r;
  }
  return [left, right];
}
