/**
 * The audio backend boundary — SPEC §6, build-order step 5.
 *
 * ## Where the fidelity line falls, and why
 *
 * The original is Sound Manager all the way down: `SndNewChannel`, `bufferCmd`,
 * double-buffer callbacks, `SoundConverter` for format I/O. None of that is
 * observable to a user, and none of it is what makes SoundEffects *sound like
 * SoundEffects* — the character lives in the **data**, which steps 2–4 already
 * reproduce byte-for-byte (offset-binary, integer DSP, truncating divides,
 * drop-sample resampling scars). Playback is where that data finally leaves the
 * building; it is not where the identity is made.
 *
 * So the mechanism is modernised — a pull-based render callback behind this
 * interface, the same shape as `GlueRec`: a browser host implements it over Web
 * Audio, a test implements it over an array, a Daisy implements it over a codec.
 * The core stays headless and testable, and §12b's "the same code runs on
 * hardware later" stays true.
 *
 * What is *not* modernised is anything a user could notice:
 *
 *  - **The declared rate is obeyed, even when it is a lie.** This is the load-
 *    bearing one. Change Sound Info (§5) and Keyboard's Change Pitch (§8 #18)
 *    both work by editing the header and touching no audio; if playback silently
 *    "corrected" a bogus rate, both features would evaporate. Pitch mangling via
 *    header lies is a feature, not a bug to route around.
 *  - Per-channel L/R speaker routing, 8-step volume, solo/mute, loop semantics,
 *    play-from preference, unequal channels simply ending — all preserved.
 *
 * Two deliberate divergences, called out rather than buried:
 *
 *  1. **Resampling to the device rate is the backend's**, not a reimplementation
 *     of whatever the Sound Manager did. A modern resampler makes an 8-bit 22 kHz
 *     sound audibly *cleaner* on the way out than a IIci did. The document's bytes
 *     are untouched either way, so this is not the destructive-scars identity —
 *     but it is a real choice, and a backend that wants period-correct grit can
 *     make its own. (Note the contrast with `ModResampleSoundRate`, which *is*
 *     ported exactly, because that one rewrites the document.)
 *  2. **The 16-bit hardware gate is a capability query**, not a Gestalt flag. On
 *     any modern backend `supports16Bit` is true and the alert never fires, which
 *     makes it feel like dead code — but it is honest dead code: a genuinely
 *     limited backend (a Daisy build, a deliberately hobbled demo) can report
 *     false and get the original's voice back, verbatim.
 */

import type { SoundDocument } from "../document.js";

/**
 * What a backend can do. The modern replacement for `MachineRec`'s pre-resolved
 * Gestalt flags (§7.6) — same idea, asked of the output device instead of the
 * ROM.
 */
export interface AudioCapabilities {
  /** The device's own sample rate, Hz. The backend resamples to it. */
  deviceRate: number;
  /** False makes the "not enough hardware" alert fire, as on a real 8-bit Mac. */
  supports16Bit: boolean;
  /** False collapses L/R routing to a mono sum, as `mixesStereoOutputToMono` did. */
  supportsStereo: boolean;
  /** Whether recording is available at all (`hasSoundInputDevice`). */
  supportsInput: boolean;
  /** Whether play-through / monitoring while recording works (`hasPlayAndRecord`). */
  supportsPlayAndRecord: boolean;
}

/**
 * "There is not enough hardware to play the selected sound. You may want to
 * downsample it before playing." `[RES]` — verbatim, and the reason
 * `supports16Bit` exists at all.
 */
export const NOT_ENOUGH_HARDWARE =
  "There is not enough hardware to play the selected sound. You may want to downsample it before playing.";

/**
 * Can this document be played on this backend? (§6)
 *
 * The original gates on bit depth alone. Note it asks about the *document*, not
 * the selection, and note what it does when the answer is no: it does not
 * downsample for you, it tells you to go do it yourself. That refusal is the app.
 */
export function canPlay(doc: SoundDocument, caps: AudioCapabilities): boolean {
  return doc.bps <= 8 || caps.supports16Bit;
}

/**
 * A source of audio the backend pulls from. Implemented by `Transport`.
 *
 * Pull-based on purpose: it is what every modern audio API wants, it makes the
 * whole engine testable by calling `render` in a loop with no device present, and
 * it is the shape a Daisy's block callback already has.
 */
export interface AudioSource {
  /**
   * Fill `out` (one Float32Array per output channel, interleaved by nothing —
   * planar) with the next `frames` frames at `deviceRate`.
   *
   * @returns frames actually written. Fewer than requested means the source is
   *          finished, and the backend should stop.
   */
  render(out: Float32Array[], frames: number): number;
}

/**
 * The output device. A browser host implements this over an AudioWorklet; the
 * tests implement it over an array.
 */
export interface AudioBackend {
  readonly capabilities: AudioCapabilities;
  /** Begin pulling from `source`. Replaces any current source. */
  start(source: AudioSource): void;
  stop(): void;
  readonly isRunning: boolean;
}

/**
 * An input device, for recording. Push-based, because that is how capture APIs
 * work — `getUserMedia`, a codec's RX callback, a Daisy's ADC block.
 */
export interface AudioInput {
  /** Planar float frames at `rate`, one array per input channel (1 or 2). */
  onFrames(cb: (channels: Float32Array[], rate: number) => void): void;
  start(): void;
  stop(): void;
  readonly isRunning: boolean;
}

/** Capabilities of a machine that can do everything — the modern default. */
export const MODERN_CAPABILITIES: AudioCapabilities = {
  deviceRate: 48000,
  supports16Bit: true,
  supportsStereo: true,
  supportsInput: true,
  supportsPlayAndRecord: true,
};

/**
 * Capabilities of a machine that cannot manage 16-bit output — a Mac Plus, or a
 * hardware build that wants the original's refusal back.
 */
export const EIGHT_BIT_CAPABILITIES: AudioCapabilities = {
  deviceRate: 22254.5454,
  supports16Bit: false,
  supportsStereo: false,
  supportsInput: true,
  supportsPlayAndRecord: false,
};

/**
 * A backend that renders into an array instead of a speaker. Not a mock — it is
 * the backend the tests and any offline-render path actually use, and it is the
 * proof that the engine has no hidden device dependency.
 */
export class OfflineBackend implements AudioBackend {
  readonly capabilities: AudioCapabilities;
  private source: AudioSource | null = null;
  private running = false;

  constructor(capabilities: Partial<AudioCapabilities> = {}) {
    this.capabilities = { ...MODERN_CAPABILITIES, ...capabilities };
  }

  start(source: AudioSource): void {
    this.source = source;
    this.running = true;
  }

  stop(): void {
    this.running = false;
    this.source = null;
  }

  get isRunning(): boolean {
    return this.running;
  }

  /**
   * Pull everything the source has, in `blockSize` chunks, and return the whole
   * thing. `maxFrames` is a guard: a looping transport never finishes.
   */
  renderAll(channels = 2, blockSize = 512, maxFrames = 48000 * 60): Float32Array[] {
    const acc: number[][] = Array.from({ length: channels }, () => []);
    const block = Array.from({ length: channels }, () => new Float32Array(blockSize));
    let total = 0;

    while (this.running && this.source && total < maxFrames) {
      for (const b of block) b.fill(0);
      const written = this.source.render(block, blockSize);
      for (let c = 0; c < channels; c++) {
        for (let i = 0; i < written; i++) acc[c]!.push(block[c]![i]!);
      }
      total += written;
      if (written < blockSize) break;
    }
    return acc.map((a) => Float32Array.from(a));
  }
}
