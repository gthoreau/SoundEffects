/**
 * VOX (voice-activated) recording — SPEC §6. `[RES: CNTL 307–313]`
 *
 * "recording start trigger On/Off with threshold slider (0–255), independent stop
 * trigger with threshold and **stop-delay** slider. A destructive weirdo sampler
 * wants this."
 *
 * The controls are recovered from the resource fork; the *state machine* between
 * them is not, so everything below is a reading of what those seven CNTLs can
 * mean together. It is a small machine and the sliders constrain it hard, but it
 * is `[INTERPRETED]` — see SPEC §14.
 *
 * Two design notes that follow from the controls themselves rather than from
 * taste:
 *
 *  - **Start and stop triggers are independent.** Each has its own On/Off and its
 *    own threshold, so stop-threshold > start-threshold is legal and means
 *    "record until it gets *loud*". That is not a misconfiguration to guard
 *    against; it is a patch.
 *  - **Only the stop trigger has a delay.** There is no start delay, so the start
 *    fires on the first sample over threshold and the attack is never clipped.
 *    The stop delay is what stops a pause between words ending the take.
 *
 * §12b: "VOX record start/stop with thresholds and stop-delay is directly
 * patchable (gate in)" — this machine is the gate logic, and it is deliberately
 * free of any audio-API assumptions so a Daisy can run it verbatim.
 */

/** The machine's states, in the order a take moves through them. */
export const VoxState = {
  /** Not armed. Not listening. */
  idle: "idle",
  /** Armed and listening, but below the start threshold. Nothing is kept. */
  monitoring: "monitoring",
  /** Above the start threshold. Frames are being kept. */
  recording: "recording",
  /** Below the stop threshold, waiting out the stop-delay. Still keeping frames. */
  stopping: "stopping",
  /** The take ended. */
  finished: "finished",
} as const;

export type VoxStateName = (typeof VoxState)[keyof typeof VoxState];

export interface VoxSettings {
  /** Start trigger On/Off. Off means recording begins the moment you arm. */
  startTrigger: boolean;
  /** 0–255. */
  startThreshold: number;
  /** Stop trigger On/Off. Off means only Stop ends the take. */
  stopTrigger: boolean;
  /** 0–255. */
  stopThreshold: number;
  /**
   * How long the level must stay below `stopThreshold` before the take ends,
   * in **frames**. The slider is a duration; the host converts.
   */
  stopDelayFrames: number;
}

export const DEFAULT_VOX: VoxSettings = {
  startTrigger: false,
  startThreshold: 32,
  stopTrigger: false,
  stopThreshold: 32,
  stopDelayFrames: 0,
};

/**
 * Input level on the sliders' own 0–255 scale.
 *
 * `[INTERPRETED]`: the sliders are 0–255 `[RES]`; what they are 0–255 *of* is not
 * recorded. Peak magnitude scaled to full range is the reading — it is what a
 * hardware VOX does, it makes 0 = "trigger on anything" and 255 = "never trigger"
 * both sensible, and it matches the 8-bit world the app was born in, where a
 * sample magnitude *is* 0–127ish and the slider reads like a sample value.
 */
export function inputLevel(frame: Float32Array, offset = 0, count = 1): number {
  let peak = 0;
  for (let i = offset; i < Math.min(offset + count, frame.length); i++) {
    const v = Math.abs(frame[i]!);
    if (v > peak) peak = v;
  }
  return Math.min(255, Math.round(peak * 255));
}

/**
 * The VOX gate. Feed it a level per block; it tells you whether to keep the audio.
 *
 * Deliberately level-in / decision-out with no buffers of its own: the recorder
 * owns the audio, this owns only the decision. That is what makes it portable to
 * a gate input.
 */
export class Vox {
  private state: VoxStateName = VoxState.idle;
  /** Frames spent below the stop threshold so far. */
  private belowFor = 0;

  constructor(private settings: VoxSettings = { ...DEFAULT_VOX }) {}

  get currentState(): VoxStateName {
    return this.state;
  }

  /** Are we keeping audio right now? True while recording *and* while stopping. */
  get isKeeping(): boolean {
    return this.state === VoxState.recording || this.state === VoxState.stopping;
  }

  update(settings: Partial<VoxSettings>): void {
    this.settings = { ...this.settings, ...settings };
  }

  /**
   * Arm. With the start trigger off, recording begins immediately — the trigger
   * is opt-in, so an un-configured VOX behaves like a plain record button.
   */
  arm(): void {
    this.belowFor = 0;
    this.state = this.settings.startTrigger ? VoxState.monitoring : VoxState.recording;
  }

  /** The Stop button. Ends the take whatever the triggers think. */
  stop(): void {
    this.state = VoxState.finished;
  }

  reset(): void {
    this.state = VoxState.idle;
    this.belowFor = 0;
  }

  /**
   * Advance the machine by `frames` frames at the given level.
   *
   * @returns true if this block's audio should be kept.
   */
  process(level: number, frames = 1): boolean {
    const s = this.settings;

    switch (this.state) {
      case VoxState.monitoring:
        // No start delay: the first sample over threshold starts the take, so the
        // attack survives.
        if (level >= s.startThreshold) {
          this.state = VoxState.recording;
          this.belowFor = 0;
          return true;
        }
        return false;

      case VoxState.recording:
        if (s.stopTrigger && level < s.stopThreshold) {
          this.state = VoxState.stopping;
          this.belowFor = frames;
          // A zero delay ends it immediately.
          if (this.belowFor >= s.stopDelayFrames) this.state = VoxState.finished;
        }
        return true;

      case VoxState.stopping:
        if (level >= s.stopThreshold) {
          // Back over the threshold: the take continues, and the delay resets.
          // This is what keeps a pause between words from ending a recording.
          this.state = VoxState.recording;
          this.belowFor = 0;
          return true;
        }
        this.belowFor += frames;
        if (this.belowFor >= s.stopDelayFrames) {
          this.state = VoxState.finished;
          // The frames spent waiting out the delay are KEPT — they were recorded
          // while the machine was still deciding, and throwing them away would
          // clip the tail of every take.
          return true;
        }
        return true;

      case VoxState.idle:
      case VoxState.finished:
      default:
        return false;
    }
  }
}
