/**
 * The transport — SPEC §3 (the tape-recorder buttons), §6, §4's keyboard map.
 *
 * Record · Stop · Pause · Play · Fast Forward · Loop · Volume.
 *
 * This is where "the declared rate is obeyed, even when it is a lie" actually
 * bites. The playback position advances through the document at
 * `doc.rate / deviceRate` bytes per output frame — `doc.rate` being whatever the
 * header *claims*, not whatever the audio really is. Change Sound Info (§5) and
 * Keyboard's Change Pitch (§8 #18) are both nothing but edits to that field, and
 * they work precisely because this line trusts it.
 */

import type { SoundDocument } from "../document.js";
import { frameSizeFor } from "../samples.js";
import { fixedToDouble } from "../units.js";
import type { AudioCapabilities, AudioSource } from "./backend.js";
import { mixFrame, newMonitor, VOLUME_FULL, type ChannelMonitor } from "./mixer.js";

/** The transport buttons' states. */
export const TransportState = {
  stopped: "stopped",
  playing: "playing",
  paused: "paused",
} as const;

export type TransportStateName = (typeof TransportState)[keyof typeof TransportState];

/** "Start playing from • Beginning / • Insertion Point" — Preferences page 1. `[RES]` */
export const PlayFrom = { beginning: 1, insertionPoint: 2 } as const;

export interface TransportOptions {
  capabilities: AudioCapabilities;
  /** Preferences page 1. */
  playFrom?: number;
  /** 0–7. Off, 1–7. */
  volume?: number;
  monitor?: ChannelMonitor;
}

export interface PlayOptions {
  /**
   * Loop button: loops the **selection**. `[DOC]`
   * With `useLoopRegion`, loops the **Set Loop region** instead — that is
   * option-Play (§4).
   */
  loop?: boolean;
  /** option-Play: loop the Set Loop region rather than the selection. */
  useLoopRegion?: boolean;
  /**
   * A rate multiplier applied on top of the document's declared rate. Keyboard's
   * chromatic auditioning is nothing but this (§8 #18).
   */
  rateScale?: number;
  /** Play only this 0-based channel — F1–Fn soloing (§4). */
  soloChannel?: number;
}

/**
 * The playback engine. An `AudioSource`, so a backend just pulls on it.
 *
 * Deliberately not a class that owns a device: it owns a *position* and knows how
 * to fill a buffer. Everything device-shaped lives behind `AudioBackend`.
 */
export class Transport implements AudioSource {
  private state: TransportStateName = TransportState.stopped;
  /** Byte offset into the document. Fractional: the device rate rarely divides. */
  private pos = 0;
  private playOpts: PlayOptions = {};
  private loopStart = 0;
  private loopEnd = 0;
  private playEnd = 0;

  readonly capabilities: AudioCapabilities;
  playFrom: number;
  volume: number;
  monitor: ChannelMonitor;

  constructor(
    private readonly doc: SoundDocument,
    opts: TransportOptions,
  ) {
    this.capabilities = opts.capabilities;
    this.playFrom = opts.playFrom ?? PlayFrom.beginning;
    this.volume = opts.volume ?? VOLUME_FULL;
    this.monitor = opts.monitor ?? newMonitor();
  }

  get currentState(): TransportStateName {
    return this.state;
  }

  /** Byte offset of the playhead, frame-aligned down. */
  get position(): number {
    const frameSize = frameSizeFor(this.doc.bps);
    const p = Math.floor(this.pos);
    return p - (p % frameSize);
  }

  /**
   * Bytes of the document consumed per output frame.
   *
   * `doc.rate` is the **declared** rate. If the header lies, the pitch is wrong,
   * and that is the entire point of Change Sound Info and Change Pitch.
   */
  private get step(): number {
    const frameSize = frameSizeFor(this.doc.bps);
    const declared = fixedToDouble(this.doc.rate) * (this.playOpts.rateScale ?? 1);
    return (declared / this.capabilities.deviceRate) * frameSize;
  }

  /** Play. Honours the play-from preference and the loop options. */
  play(opts: PlayOptions = {}): void {
    this.playOpts = opts;

    // option-Play loops the Set Loop region; the plain Loop button loops the
    // selection.
    if (opts.loop && opts.useLoopRegion) {
      this.loopStart = this.doc.loopSt;
      this.loopEnd = this.doc.loopEnd;
    } else if (opts.loop) {
      this.loopStart = this.doc.selSt;
      this.loopEnd = this.doc.selEnd;
    } else {
      this.loopStart = 0;
      this.loopEnd = 0;
    }

    // A degenerate loop region (never set, or collapsed) falls back to the whole
    // sound rather than spinning on nothing.
    if (opts.loop && this.loopEnd <= this.loopStart) {
      this.loopStart = 0;
      this.loopEnd = this.doc.maxChSize();
    }

    if (opts.loop) {
      this.pos = this.loopStart;
      this.playEnd = this.loopEnd;
    } else {
      // A real selection plays just the selection; a caret plays from the caret
      // (or the beginning) to the end of the sound.
      const hasSelection = this.doc.selSt !== this.doc.selEnd;
      if (hasSelection) {
        this.pos = this.doc.selSt;
        this.playEnd = this.doc.selEnd;
      } else {
        this.pos = this.playFrom === PlayFrom.insertionPoint ? this.doc.selSt : 0;
        this.playEnd = this.doc.maxChSize();
      }
    }

    if (opts.soloChannel !== undefined) {
      this.monitor.soloed.clear();
      this.monitor.soloed.add(opts.soloChannel);
    }

    this.state = TransportState.playing;
  }

  /** Stop. Unlike Pause, it forgets where it was. */
  stop(): void {
    this.state = TransportState.stopped;
    this.pos = 0;
  }

  /** Pause. Keeps the playhead. Space is the play/pause toggle (§4). */
  pause(): void {
    if (this.state === TransportState.playing) this.state = TransportState.paused;
    else if (this.state === TransportState.paused) this.state = TransportState.playing;
  }

  /**
   * Fast Forward. `[RES: the transport button; DOC-thin]`
   *
   * `[INTERPRETED]`: the button exists (§3) but its behaviour is not written down
   * anywhere recovered. On a tape recorder it winds; here it seeks the playhead
   * toward the end of the play range. Seeking rather than scrubbing-with-audio is
   * the guess — audible fast-forward would need a whole scrub path that nothing
   * in the resource fork hints at. See SPEC §14.
   */
  fastForward(bytes?: number): void {
    const jump = bytes ?? Math.max(1, Math.floor((this.playEnd - this.pos) / 4));
    this.pos = Math.min(this.pos + jump, this.playEnd);
  }

  /** Move the playhead. Frame-aligned, as every byte offset in this app is. */
  seek(bytes: number): void {
    const frameSize = frameSizeFor(this.doc.bps);
    const p = Math.max(0, Math.min(bytes, this.doc.maxChSize()));
    this.pos = p - (p % frameSize);
  }

  /** `AudioSource` — the backend pulls this. */
  render(out: Float32Array[], frames: number): number {
    const left = out[0];
    const right = out[1] ?? out[0];
    if (!left || !right) return 0;

    if (this.state !== TransportState.playing) return 0;

    const frameSize = frameSizeFor(this.doc.bps);
    const step = this.step;
    const looping = this.playOpts.loop === true;
    const forceMono = !this.capabilities.supportsStereo;

    let written = 0;
    for (let i = 0; i < frames; i++) {
      if (this.pos >= this.playEnd) {
        if (!looping) {
          this.state = TransportState.stopped;
          break;
        }
        // Wrap. Carry the fraction so a loop whose length isn't a whole number
        // of device frames doesn't drift.
        const span = this.playEnd - this.loopStart;
        if (span <= 0) {
          this.state = TransportState.stopped;
          break;
        }
        this.pos = this.loopStart + ((this.pos - this.loopStart) % span);
      }

      // Zero-order hold: the read is frame-aligned by truncation, which is the
      // same drop-sample the host resampler does (Host-Resamplers.md) — here it
      // is a device-rate accommodation rather than a document edit, so it leaves
      // no scar behind.
      let read = Math.floor(this.pos);
      read -= read % frameSize;

      const [l, r] = mixFrame(this.doc, read, {
        volume: this.volume,
        monitor: this.monitor,
        forceMono,
      });
      left[i] = l;
      right[i] = r;

      this.pos += step;
      written++;
    }
    return written;
  }
}
