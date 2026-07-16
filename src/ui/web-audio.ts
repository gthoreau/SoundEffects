/**
 * The Web Audio backend — the browser's implementation of `AudioBackend` (§6).
 *
 * Step 5 argued the fidelity line at the top of `src/audio/backend.ts` and then
 * shipped only `OfflineBackend`, which renders into an array for the tests. This
 * is the other implementation: the one with a speaker on the end of it. It adds
 * no behaviour — `Transport` already owns loop semantics, the 8-step volume
 * ladder, L/R routing and play-from — it only carries frames to the device.
 *
 * **Why `ScriptProcessorNode`, which is deprecated.** `Transport` is a
 * *pull* source (`render(out, frames)`) that reads the live `SoundDocument`, and
 * the document is owned by the main thread — the editor mutates it destructively
 * on every effect and every Cut. An `AudioWorklet` runs on the audio thread, so
 * using one means either shipping the document across a port on every edit or
 * moving the model off the main thread. Both trade a real architectural change
 * for latency this app does not care about: a program whose entire premise is a
 * 22 kHz 8-bit sample buffer is not one where a few ms of jitter is the problem.
 * `ScriptProcessorNode` matches the pull model exactly and costs nothing. If the
 * glitching ever matters, the fix is a worklet plus a ring buffer, and
 * `AudioBackend` is the seam it goes behind — which is the whole point of the
 * seam.
 *
 * The `deviceRate` is whatever the browser gives us (usually 44.1 or 48 kHz),
 * and `Transport.step` resamples the document's *declared* rate to it — badly,
 * on purpose, by dropping samples. That is the one deliberate divergence step 5
 * called out: a modern resampler would make an 8-bit 22 kHz sound cleaner than a
 * IIci ever did.
 */

import type { AudioBackend, AudioCapabilities, AudioSource } from "../audio/backend.js";
import { MODERN_CAPABILITIES } from "../audio/backend.js";

/** Frames per `onaudioprocess` callback. 2048 ≈ 46 ms at 44.1 kHz. */
const BUFFER_SIZE = 2048;

export interface WebAudioBackendOptions {
  /** Overrides — `supports16Bit: false` gets the original's refusal back (§6). */
  capabilities?: Partial<AudioCapabilities>;
  /** Fired when the source runs out, so the UI can drop back to Stopped. */
  onEnded?: () => void;
}

/**
 * `AudioBackend` over Web Audio.
 *
 * The context is created lazily and resumed on demand: browsers refuse to start
 * one without a user gesture, and the first gesture here is always a click on
 * Play or a keypress of space — so `start()` is exactly the right place.
 */
export class WebAudioBackend implements AudioBackend {
  readonly capabilities: AudioCapabilities;

  private ctx: AudioContext | null = null;
  private node: ScriptProcessorNode | null = null;
  private source: AudioSource | null = null;
  private running = false;
  private onEnded: (() => void) | undefined;

  constructor(opts: WebAudioBackendOptions = {}) {
    this.onEnded = opts.onEnded;
    // deviceRate is a placeholder until the context exists and reports its own.
    this.capabilities = { ...MODERN_CAPABILITIES, ...opts.capabilities };
  }

  /** True once the browser has handed us a context. */
  get isRunning(): boolean {
    return this.running;
  }

  /** The live context's rate, which is the one `Transport` must resample to. */
  get deviceRate(): number {
    return this.ctx?.sampleRate ?? this.capabilities.deviceRate;
  }

  /**
   * Create the device up front, without starting it.
   *
   * `Transport` resamples the document's declared rate to `deviceRate`, so the
   * rate has to be a fact *before* a Transport is constructed — and only the
   * context can tell us what it is. A context may be created before any user
   * gesture; it just starts suspended, which `start()` resolves.
   */
  prepare(): void {
    this.ensureContext();
  }

  private ensureContext(): AudioContext {
    if (this.ctx) return this.ctx;
    const Ctor: typeof AudioContext =
      window.AudioContext ??
      (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
    const ctx = new Ctor();
    this.ctx = ctx;

    // The device's rate is a fact, not a preference: whatever it is, Transport
    // resamples to it. Overwrite the placeholder now that we can ask.
    this.capabilities.deviceRate = ctx.sampleRate;

    // Two output channels always — §6's router decides what goes in them, and a
    // mono document simply lights both. Asking for 1 would delete L/R routing.
    const node = ctx.createScriptProcessor(BUFFER_SIZE, 0, 2);
    node.onaudioprocess = (e) => this.process(e);
    node.connect(ctx.destination);
    this.node = node;
    return ctx;
  }

  private process(e: AudioProcessingEvent): void {
    const out = e.outputBuffer;
    const left = out.getChannelData(0);
    const right = out.getChannelData(1);

    if (!this.source || !this.running) {
      left.fill(0);
      right.fill(0);
      return;
    }

    const written = this.source.render([left, right], out.length);

    // A short read means the source has no more to give. Silence the tail rather
    // than leaving the previous block's frames in it — Web Audio reuses the
    // buffer, so a partial write would repeat whatever was there before.
    //
    // Note what this cannot distinguish: `Transport.render` returns 0 both when
    // playback has *finished* and when it is merely *paused*. The AudioSource
    // contract only says "fewer than requested means stop pulling", which is
    // correct in both cases — but it means `onEnded` fires on a pause too, so
    // the listener must ask the transport which of the two happened. App.ts does.
    if (written < out.length) {
      left.fill(0, written);
      right.fill(0, written);
      this.running = false;
      this.source = null;
      // Defer: we are on the audio callback, and no UI may run here.
      queueMicrotask(() => this.onEnded?.());
    }
  }

  start(source: AudioSource): void {
    const ctx = this.ensureContext();
    this.source = source;
    this.running = true;
    // Autoplay policy: a context created before a gesture starts suspended, and
    // stays suspended until something resumes it.
    if (ctx.state === "suspended") void ctx.resume();
  }

  stop(): void {
    this.running = false;
    this.source = null;
  }

  /** Release the device. The editor keeps working; it just goes quiet. */
  close(): void {
    this.stop();
    this.node?.disconnect();
    this.node = null;
    void this.ctx?.close();
    this.ctx = null;
  }
}
