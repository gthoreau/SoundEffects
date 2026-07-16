/**
 * The document model — SPEC §2.1, shaped to match `ModParamsRec` (§7.3) so the
 * effects runtime (step 3) can hand a module the live state without a
 * translation layer.
 *
 * The load-bearing weirdness, and the reason this is not just an AudioBuffer:
 * **every channel is an independent buffer with its own length.** Nothing here
 * may assume channels are equal length, pad them to match, or "repair" them.
 * Tracks drifting out of alignment is a feature (§5).
 */

import { frameSizeFor, silentByte } from "./samples.js";
import { Unit, type Fixed, type UnitCode, RATE_22K } from "./units.js";

/** 8.8 fixed volume: 0x0100 = 1.0. >1.0 overdrives, <=0 is silent. `[SOURCE]` */
export const VOLUME_UNITY = 0x0100;

/** SPEC §2.1. One lane of the wave editor; `size` is bytes, never samples. */
export interface Channel {
  data: Uint8Array;
  /** Speaker enable, left. Drives Quick Mix routing too. */
  outLeft: boolean;
  outRight: boolean;
  /** 8.8 fixed. */
  volume: number;
}

/** A named region. Kinds seen in the app: "Chunk" and "Clipping". `[RES]` */
export interface Chunk {
  name: string;
  start: number;
  length: number;
  /** 1-based, matching the app's channel numbering. */
  channel: number;
  kind: "Chunk" | "Clipping";
}

export interface DocumentInit {
  name?: string;
  rate?: Fixed;
  bps?: number;
  baseFreq?: number;
  numChans?: number;
  /** Initial length of each channel, in bytes. */
  sizeBytes?: number;
}

/**
 * A sound. Mutable and destructive by design — there is no undo here and there
 * must never be one (§1). Every edit rewrites buffers in place.
 */
export class SoundDocument {
  name: string;
  /** 16.16 fixed-point Hz. */
  rate: Fixed;
  /** Bits per sample, 1..32. Lying about this is a feature (§5). */
  bps: number;
  /** MIDI base note for playback pitch. */
  baseFreq: number;
  channels: Channel[];

  /** Byte offsets, frame-aligned, half-open [selSt, selEnd). Equal = caret. */
  selSt = 0;
  selEnd = 0;
  /** 1-based, contiguous. `[SOURCE: Gain.c loops firstSelCh-1 .. lastSelCh-1]` */
  firstSelCh = 1;
  lastSelCh = 1;

  /** Set Loop region, byte offsets. */
  loopSt = 0;
  loopEnd = 0;

  chunks: Chunk[] = [];
  /** Current display unit. Remembered per document. `[BINARY]` */
  units: UnitCode = Unit.seconds;

  constructor(init: DocumentInit = {}) {
    this.name = init.name ?? "untitled sound #1";
    this.rate = init.rate ?? RATE_22K;
    this.bps = init.bps ?? 8;
    this.baseFreq = init.baseFreq ?? 60;
    const numChans = init.numChans ?? 1;
    const sizeBytes = init.sizeBytes ?? 0;
    this.channels = [];
    for (let i = 0; i < numChans; i++) this.channels.push(this.newChannel(sizeBytes));
    this.lastSelCh = numChans;
  }

  /** `frameSize = ceil(bps/8)`. */
  get frameSize(): number {
    return frameSizeFor(this.bps);
  }

  get numChans(): number {
    return this.channels.length;
  }

  private newChannel(sizeBytes: number): Channel {
    const data = new Uint8Array(sizeBytes);
    data.fill(silentByte(this.bps));
    return { data, outLeft: true, outRight: true, volume: VOLUME_UNITY };
  }

  /** `ModMaxChSize` — max size over *all* channels. `[SOURCE: Glue.h]` */
  maxChSize(): number {
    let max = 0;
    for (const ch of this.channels) if (ch.data.length > max) max = ch.data.length;
    return max;
  }

  /** `ModMaxRelChSize` — max size over the *selected* channels. `[SOURCE: Glue.h]` */
  maxRelChSize(): number {
    let max = 0;
    for (const ch of this.selectedChannels()) {
      if (ch.data.length > max) max = ch.data.length;
    }
    return max;
  }

  /** The selected channels, in document order. Honours the 1-based range. */
  selectedChannels(): Channel[] {
    return this.channels.slice(this.firstSelCh - 1, this.lastSelCh);
  }

  /** Indices (0-based) of the selected channels. */
  selectedChannelIndices(): number[] {
    const out: number[] = [];
    for (let i = this.firstSelCh - 1; i <= this.lastSelCh - 1; i++) out.push(i);
    return out;
  }

  /**
   * `ModAddNewChannel(modData, where)` — `where` is a 1-based insert position;
   * the new channel is silent and zero-length, matching "drag New Channel into
   * the column" (§3). Defaults to appending.
   */
  addChannel(where: number = this.numChans + 1, sizeBytes = 0): Channel {
    const ch = this.newChannel(sizeBytes);
    const idx = Math.max(0, Math.min(this.numChans, where - 1));
    this.channels.splice(idx, 0, ch);
    this.clampChannelSelection();
    return ch;
  }

  /**
   * `ModDelChannel(modData, chan)` — 1-based.
   *
   * Refuses to delete the last channel: the app says "I'm sorry, but I can't
   * delete the only channel of a mono sound. Delete the document instead." (§3).
   * The refusal is the caller's to voice; we just decline.
   */
  delChannel(chan: number): boolean {
    if (this.numChans <= 1) return false;
    if (chan < 1 || chan > this.numChans) return false;
    this.channels.splice(chan - 1, 1);
    this.chunks = this.chunks.filter((c) => c.channel !== chan);
    for (const c of this.chunks) if (c.channel > chan) c.channel--;
    this.clampChannelSelection();
    return true;
  }

  /** Drag one channel icon onto another → swap positions (§3). 1-based. */
  swapChannels(a: number, b: number): void {
    if (a < 1 || b < 1 || a > this.numChans || b > this.numChans) return;
    const ca = this.channels[a - 1]!;
    this.channels[a - 1] = this.channels[b - 1]!;
    this.channels[b - 1] = ca;
  }

  private clampChannelSelection(): void {
    this.firstSelCh = Math.max(1, Math.min(this.firstSelCh, this.numChans));
    this.lastSelCh = Math.max(this.firstSelCh, Math.min(this.lastSelCh, this.numChans));
  }

  /** A deep copy — used for the clipboard, which holds real sound (§5). */
  clone(): SoundDocument {
    const doc = new SoundDocument({
      name: this.name,
      rate: this.rate,
      bps: this.bps,
      baseFreq: this.baseFreq,
      numChans: 0,
    });
    doc.channels = this.channels.map((ch) => ({
      data: ch.data.slice(),
      outLeft: ch.outLeft,
      outRight: ch.outRight,
      volume: ch.volume,
    }));
    doc.selSt = this.selSt;
    doc.selEnd = this.selEnd;
    doc.firstSelCh = this.firstSelCh;
    doc.lastSelCh = this.lastSelCh;
    doc.loopSt = this.loopSt;
    doc.loopEnd = this.loopEnd;
    doc.units = this.units;
    doc.chunks = this.chunks.map((c) => ({ ...c }));
    return doc;
  }
}
