/**
 * The canonical `effect()` skeleton — SPEC §7.8, `[SOURCE: Gain.c]`.
 *
 * "Every recovered module in §8 is a variation on this skeleton — the
 * differences are the inner kernel and whether the pass runs forward/backward,
 * extends the buffer, or appends channels." So this file is the plugin base
 * class the build order asks for, and modules below it should read like the C.
 *
 * The steps, in Gain.c's order, because the order is load-bearing:
 *   1. own resource file          5. clamp to THIS channel's size, frame-align
 *   2. read & free settings       6. the inner kernel  ← the only bit that varies
 *   3. progress text              7. saturating clamp
 *   4. empty selection ⇒ whole    8. poll progress (usually canCancel = false)
 */

import type { SoundDocument } from "../document.js";
import { frameSizeFor, getSample, setSample } from "../samples.js";
import { channelRange, expandEmptySelection } from "../selection.js";
import type { Host } from "./host.js";
import { kModCancel, kModNoError, type ModResult } from "./results.js";

/** One channel's clamped, frame-aligned range — Gain.c step 5. */
export interface ChannelPass {
  /** 0-based, as Gain.c's `channel` loop variable. */
  index: number;
  data: Uint8Array;
  start: number;
  end: number;
}

/**
 * The per-channel loop: `for (channel = firstSelCh-1; channel < lastSelCh; channel++)`
 * with each channel clamped to its own size and frame-aligned.
 *
 * Yields nothing for a channel whose range is empty (Gain.c's `if (start != end)`),
 * which is exactly how a channel shorter than the selection gets skipped.
 */
export function* channelPasses(doc: SoundDocument): Generator<ChannelPass> {
  for (let channel = doc.firstSelCh - 1; channel < doc.lastSelCh; channel++) {
    const ch = doc.channels[channel];
    if (!ch) continue;
    const { start, end } = channelRange(doc, channel);
    if (start === end) continue;
    yield { index: channel, data: ch.data, start, end };
  }
}

/**
 * The progress dance, which is fiddly enough to be worth encapsulating: the
 * module polls `timeToCallProgress` every sample and only calls `showProgress`
 * when the host has raised it. Returns true when the module should break out —
 * **leaving the half-processed audio in place** (§1.2).
 */
export class Progress {
  private processed = 0;

  constructor(
    private readonly host: Host,
    private readonly total: number,
    private readonly canCancel: boolean,
  ) {}

  /** Call once per sample, exactly where Gain.c calls it: after the write. */
  tick(): boolean {
    this.processed++;
    if (!this.host.timeToCallProgress) return false;
    return this.host.showProgress(this.processed, this.total, this.canCancel) !== kModNoError;
  }

  get count(): number {
    return this.processed;
  }
}

export interface SkeletonOptions {
  /** The `STR ' 128` progress text, e.g. "Amplifying by ". */
  text?: string;
  /**
   * Almost always false. Amplify, Echo, Delay, Noise, Smooth, Robotize and
   * Downsample Bits all hard-code `canCancel = false` — a long Echo genuinely
   * cannot be interrupted, and that is not an oversight to fix.
   */
  canCancel?: boolean;
}

/**
 * Run the skeleton, delegating each channel to `body`.
 *
 * Result follows Gain.c: start at `kModCancel` ("the module didn't do
 * anything") and promote to `kModNoError` once a channel actually had samples in
 * range. A body may return a result code to abort the whole run.
 */
export function runSkeleton(
  doc: SoundDocument,
  host: Host,
  opts: SkeletonOptions,
  body: (pass: ChannelPass, progress: Progress) => ModResult | void,
): ModResult {
  host.setupProgress(opts.text ?? "");

  expandEmptySelection(doc);

  const frameSize = frameSizeFor(doc.bps);
  const total = Math.floor(host.getBytesToProcess(doc) / frameSize);
  const progress = new Progress(host, total, opts.canCancel ?? false);

  let result: ModResult = kModCancel;
  for (const pass of channelPasses(doc)) {
    const r = body(pass, progress);
    result = kModNoError;
    if (typeof r === "number" && r !== kModNoError) {
      host.closeProgress();
      return r;
    }
  }

  host.closeProgress();
  return result;
}

/**
 * The commonest shape of all: a pointwise kernel, `s' = f(s)`, clamped.
 *
 * This is Gain.c with step 6 swapped out. Amplify, Invert and Dither are all one
 * line each on top of it.
 */
export function perSampleEffect(
  doc: SoundDocument,
  host: Host,
  opts: SkeletonOptions,
  kernel: (sample: number, pass: ChannelPass, offset: number) => number,
): ModResult {
  const { min, max } = host.getSampleValueLimits(doc.bps);
  const frameSize = frameSizeFor(doc.bps);

  return runSkeleton(doc, host, opts, (pass, progress) => {
    for (let p = pass.start; p < pass.end; p += frameSize) {
      let s = kernel(getSample(pass.data, p, doc.bps), pass, p);
      // Step 7 — saturating clamp. Gain.c tests the sign bit first; the effect
      // is a plain clamp, and the branch order is not observable.
      if (s < min) s = min;
      else if (s > max) s = max;
      setSample(pass.data, p, s, doc.bps);
      if (progress.tick()) return kModNoError; // cancelled: keep the mangled half
    }
  });
}

/**
 * Resolve the `doExtend` policy for this run. `[BINARY: Echo-Delay.md step 5]`
 *
 * The pref's own value is used directly unless it is "Ask Each Time" (2), which
 * triggers a live `AskDoExtend()` call. Echo and Delay both do exactly this.
 */
export function resolveDoExtend(host: Host): boolean {
  return host.askDoExtend();
}

/**
 * Grow a channel to `wanted` bytes and silence the new tail, degrading
 * gracefully. `[BINARY: Echo-Delay.md]`
 *
 * The original `SetHandleSize`s, then **re-queries `GetHandleSize`** — if the
 * grow only partially succeeded under memory pressure, the mix range silently
 * shrinks to whatever was achieved and no error surfaces. We have no allocator
 * that half-succeeds, so the re-query is a no-op here; the silencing is not.
 * Freshly grown handle memory is uninitialised garbage, so the tail must be
 * silenced before anything is mixed into it.
 *
 * @returns the size actually achieved.
 */
export function growAndSilence(
  doc: SoundDocument,
  channelIndex: number,
  wanted: number,
): number {
  const ch = doc.channels[channelIndex]!;
  const oldSize = ch.data.length;
  if (wanted <= oldSize) return oldSize;

  const grown = new Uint8Array(wanted);
  grown.set(ch.data, 0);
  ch.data = grown;
  // ModSilenceOut(mi, ch+1, ch+1, oldSize, wanted) — before any mix.
  const silent = doc.frameSize === 1 ? 0x80 : 0x00;
  ch.data.fill(silent, oldSize, wanted);
  return wanted;
}
