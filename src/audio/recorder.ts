/**
 * Recording — SPEC §6, §9 (Preferences pages 3 and 4). `[RES + DOC]`
 *
 * The Record window shows a live mini-waveform — **green when monitoring, red
 * while recording** — with Save (insert into document) / Cancel / Device
 * Settings…, and popups for Use (Mono/Stereo), Sampling Rate, Sampling Size,
 * Play-Through Volume, plus Auto Gain.
 *
 * The recorder owns the audio and the policy; `Vox` owns the decision. Input
 * arrives as planar floats from whatever capture API the host has, and is
 * converted to the document's packed format on the way in — the mirror of
 * `mixer.ts` converting out. Between those two edges, nothing is a float.
 */

import { SoundDocument } from "../document.js";
import { frameSizeFor, sampleValueLimits, setSample, silentByte } from "../samples.js";
import { doubleToFixed, type Fixed } from "../units.js";
import { Vox, VoxState, inputLevel, type VoxSettings, DEFAULT_VOX } from "./vox.js";

/**
 * The rates the Recording popup offers. `[RES: MENU 155]`
 *
 * Worth keeping verbatim, names and all: this list is a fossil of 1994 hardware.
 * 22.254 is the Mac's own "Standard" — the same 22254.5454 Hz that Robotize's
 * delay constant is calibrated to (`NATIVE_RATE`) — and 24 kHz is there because
 * Apple's speech recognition wanted it.
 */
export const RECORD_RATES: ReadonlyArray<{ hz: number; label: string }> = [
  { hz: 5000, label: "5 kHz" },
  { hz: 7000, label: "7 kHz" },
  { hz: 11000, label: "11 kHz" },
  { hz: 11127.2727, label: "11.1 kHz" },
  { hz: 22050, label: "22.050 kHz" },
  { hz: 22254.5454, label: "22.254 kHz (Standard)" },
  { hz: 24000, label: "24 kHz (Voice Recognition)" },
  { hz: 32000, label: "32 kHz" },
  { hz: 44100, label: "44.1 kHz (CD)" },
  { hz: 48000, label: "48 kHz (DAT)" },
];

/** Recording ▸ Use. */
export const RecordUse = { mono: 1, stereo: 2 } as const;

/**
 * The active-channels matrix — Preferences page 3. `[DOC: Intro table]`
 *
 * Mono+both = mix L/R inputs · Mono+1 = left only · Mono+2 = right only ·
 * Stereo = two separate channels.
 */
export const ActiveChannels = { both: 1, left: 2, right: 3 } as const;

/** "when recording • Insert the whole recorded sound / • Constrain to the selection" */
export const InsertPolicy = {
  whole: 1,
  /** Record *into a hole*: the take is truncated to the selection's length. */
  constrainToSelection: 2,
} as const;

export interface RecordSettings {
  /** 1 = Mono, 2 = Stereo. */
  use: number;
  /** Hz. One of RECORD_RATES. */
  rate: number;
  /** Bits per sample. */
  bps: number;
  /** 1 = both (mix), 2 = left, 3 = right. */
  activeChannels: number;
  /** Preferences page 4. */
  insertPolicy: number;
  /** "Record no more than [1000] K". */
  capKilobytes: number;
  /** "☐ only if virtual memory is active" — the cap applies only when VM is on. */
  capOnlyWithVM: boolean;
  /** Whether VM is active. Modern hosts: always true, so the cap always applies. */
  virtualMemoryActive: boolean;
  /** ☐ Beep when clipping occurs. */
  beepOnClipping: boolean;
  /** ☐ Automatic Gain Control. */
  autoGain: boolean;
  /** Play-Through Volume, 0–7. */
  playThroughVolume: number;
  vox: VoxSettings;
}

export const DEFAULT_RECORD: RecordSettings = {
  use: RecordUse.mono,
  rate: 22254.5454,
  bps: 8,
  activeChannels: ActiveChannels.both,
  insertPolicy: InsertPolicy.whole,
  capKilobytes: 1000,
  capOnlyWithVM: false,
  virtualMemoryActive: true,
  beepOnClipping: true,
  autoGain: false,
  playThroughVolume: 0,
  vox: { ...DEFAULT_VOX },
};

/** The Record window's live state — what colours the mini-waveform. */
export const RecordState = {
  /** Not armed. */
  idle: "idle",
  /** Green: listening, keeping nothing. */
  monitoring: "monitoring",
  /** Red: keeping audio. */
  recording: "recording",
  /** The take is over, awaiting Save or Cancel. */
  done: "done",
} as const;

export type RecordStateName = (typeof RecordState)[keyof typeof RecordState];

/**
 * The recorder. Push-based: the host hands it captured frames.
 *
 * It does **not** resample. The take is stored at the requested rate and the
 * document's header is set to match, so what you recorded is what the header
 * says — until you go and lie about it, which is your privilege (§5).
 */
export class Recorder {
  private state: RecordStateName = RecordState.idle;
  private readonly vox: Vox;
  /** Captured frames, one array per output channel, still as floats. */
  private captured: number[][] = [];
  private clipped = false;
  private beeps = 0;

  constructor(private settings: RecordSettings = { ...DEFAULT_RECORD }) {
    this.vox = new Vox(settings.vox);
  }

  get currentState(): RecordStateName {
    return this.state;
  }

  /** Green while monitoring, red while recording — the mini-waveform's colour. */
  get waveformColor(): "green" | "red" | null {
    if (this.state === RecordState.monitoring) return "green";
    if (this.state === RecordState.recording) return "red";
    return null;
  }

  /** Frames captured so far. */
  get frameCount(): number {
    return this.captured[0]?.length ?? 0;
  }

  /** Did the input clip during this take? */
  get didClip(): boolean {
    return this.clipped;
  }

  /** How many times the clipping beep fired. */
  get beepCount(): number {
    return this.beeps;
  }

  get voxState() {
    return this.vox.currentState;
  }

  update(settings: Partial<RecordSettings>): void {
    this.settings = { ...this.settings, ...settings };
    if (settings.vox) this.vox.update(settings.vox);
  }

  /** The byte cap, or Infinity when it doesn't apply. */
  private get capBytes(): number {
    const s = this.settings;
    if (s.capOnlyWithVM && !s.virtualMemoryActive) return Infinity;
    return s.capKilobytes * 1024;
  }

  /** Arm. Green if the VOX is waiting for a trigger, red if it starts at once. */
  start(): void {
    const outCount = this.settings.use === RecordUse.stereo ? 2 : 1;
    this.captured = Array.from({ length: outCount }, () => []);
    this.clipped = false;
    this.beeps = 0;
    this.vox.arm();
    this.state =
      this.vox.currentState === VoxState.monitoring
        ? RecordState.monitoring
        : RecordState.recording;
  }

  /** The Stop button. */
  stop(): void {
    this.vox.stop();
    this.state = RecordState.done;
  }

  /** Cancel — throw the take away. */
  cancel(): void {
    this.vox.reset();
    this.captured = [];
    this.state = RecordState.idle;
  }

  /**
   * Feed captured input. `channels` is planar float, 1 or 2 arrays.
   *
   * Routing follows the active-channels matrix, then the VOX decides whether the
   * block is kept, then the cap decides whether the take is over.
   */
  push(channels: Float32Array[]): void {
    if (this.state !== RecordState.monitoring && this.state !== RecordState.recording) {
      return;
    }
    const frames = channels[0]?.length ?? 0;
    if (frames === 0) return;

    const routed = this.route(channels, frames);

    // The VOX sees the loudest thing arriving, whatever the routing.
    let level = 0;
    for (const c of routed) level = Math.max(level, inputLevel(c, 0, c.length));

    const keep = this.vox.process(level, frames);

    if (this.vox.currentState === VoxState.recording && this.state === RecordState.monitoring) {
      this.state = RecordState.recording; // green -> red
    }

    if (keep) {
      const frameSize = frameSizeFor(this.settings.bps);
      const capFrames = Math.floor(this.capBytes / frameSize);
      for (let c = 0; c < this.captured.length; c++) {
        const src = routed[c] ?? routed[0]!;
        for (let i = 0; i < frames; i++) {
          if (this.captured[c]!.length >= capFrames) break;
          const v = src[i]!;
          // "Beep when clipping occurs" — while recording, not after. The point
          // is to tell you *now*, so you can back off the gain.
          if (this.settings.beepOnClipping && (v >= 1 || v <= -1)) {
            this.clipped = true;
            this.beeps++;
          }
          this.captured[c]!.push(v);
        }
      }
      if (this.frameCount >= capFrames) {
        this.stop();
        return;
      }
    }

    if (this.vox.currentState === VoxState.finished) {
      this.state = RecordState.done;
    }
  }

  /**
   * The active-channels matrix. `[DOC: Intro table]`
   *
   * Mono+both mixes L and R — note it **sums** them rather than averaging, which
   * is the same refusal to protect you that Quick Mix and Sum make. Recording a
   * loud stereo source in Mono+both clips, and that is the app being consistent.
   */
  private route(channels: Float32Array[], frames: number): Float32Array[] {
    const left = channels[0]!;
    // A genuinely mono input has no right channel. Substituting `left` for it
    // would make Mono+both sum the signal with ITSELF and double it — a 6 dB
    // gain out of nowhere, clipping anything past half scale.
    const right = channels[1] ?? null;

    if (this.settings.use === RecordUse.stereo) {
      // Two separate channels. A mono device gives the same signal to both.
      return [left, right ?? left];
    }
    switch (this.settings.activeChannels) {
      case ActiveChannels.left:
        return [left];
      case ActiveChannels.right:
        return [right ?? left];
      case ActiveChannels.both:
      default: {
        if (!right) return [left]; // nothing to mix it with
        const mixed = new Float32Array(frames);
        // Summed, not averaged — the same refusal to protect you that Quick Mix
        // and Sum make. A loud stereo source recorded Mono+both clips.
        for (let i = 0; i < frames; i++) mixed[i] = left[i]! + right[i]!;
        return [mixed];
      }
    }
  }

  /**
   * The take, as a document. Rate and bps are what was *requested* — the header
   * tells the truth here, and it is up to you to break it later.
   */
  toDocument(): SoundDocument {
    const bps = this.settings.bps;
    const frameSize = frameSizeFor(bps);
    const { min, max } = sampleValueLimits(bps);
    const divisor = Math.pow(2, Math.min(frameSize, 4) * 8 - 1);

    const doc = new SoundDocument({
      name: "untitled sound",
      rate: doubleToFixed(this.settings.rate) as Fixed,
      bps,
      numChans: 0,
    });

    for (const chan of this.captured) {
      const data = new Uint8Array(chan.length * frameSize);
      data.fill(silentByte(bps));
      for (let i = 0; i < chan.length; i++) {
        // Truncating, as everything in this app is.
        let v = Math.trunc(chan[i]! * divisor);
        if (v < min) v = min;
        else if (v > max) v = max;
        setSample(data, i * frameSize, v, bps);
      }
      doc.addChannel();
      doc.channels[doc.numChans - 1]!.data = data;
    }
    if (doc.numChans === 0) doc.addChannel();
    doc.firstSelCh = 1;
    doc.lastSelCh = doc.numChans;
    return doc;
  }

  /**
   * Save — insert the take into `target` at its insertion point.
   *
   * **"Constrain the recorded sound to the selection"** is the one worth
   * preserving: it truncates the take to the selection's length and *overwrites*,
   * so you record into a hole. It is how you punch a new word into a sentence
   * without shifting anything after it — and in an app with no undo, the fact
   * that it doesn't move the rest of the sound is the whole appeal.
   *
   * The take's format is not reconciled with the target's; if they differ, the
   * bytes go in as-is and the result is mangled. That is the same "just lie about
   * it" path Paste offers (§5) — see SPEC §14 item 13.
   */
  saveInto(target: SoundDocument): void {
    const take = this.toDocument();
    const frameSize = frameSizeFor(target.bps);

    if (this.settings.insertPolicy === InsertPolicy.constrainToSelection) {
      const selLen = target.selEnd - target.selSt;
      if (selLen > 0) {
        for (let c = 0; c < take.numChans; c++) {
          const dstIdx = target.firstSelCh - 1 + c;
          const dst = target.channels[dstIdx];
          if (!dst) break;
          const src = take.channels[c]!.data;
          // Overwrite in place, truncated to the hole. Nothing shifts.
          const n = Math.min(selLen, src.length, dst.data.length - target.selSt);
          for (let i = 0; i < n; i++) dst.data[target.selSt + i] = src[i]!;
        }
        return;
      }
    }

    // Insert the whole recorded sound at the caret.
    const at = target.selSt - (target.selSt % frameSize);
    for (let c = 0; c < take.numChans; c++) {
      const dstIdx = target.firstSelCh - 1 + c;
      const dst = target.channels[dstIdx];
      if (!dst) break;
      const src = take.channels[c]!.data;
      const grown = new Uint8Array(dst.data.length + src.length);
      const head = Math.min(at, dst.data.length);
      grown.set(dst.data.subarray(0, head), 0);
      grown.set(src, head);
      grown.set(dst.data.subarray(head), head + src.length);
      dst.data = grown;
    }
    target.selEnd = target.selSt;
  }
}
