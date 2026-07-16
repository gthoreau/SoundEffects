/**
 * The host services a module sees — the `GlueRec` surface. SPEC §7.6,
 * `[SOURCE: Glue.h]`.
 *
 * A module reaches everything outside its own buffer through this object. It is
 * an interface rather than a concrete class because the whole point of the ABI
 * (§12b) is that a browser host and a Daisy host present the same surface to the
 * same effect code.
 *
 * Omitted deliberately, as UI-layer concerns for step 6: `GetProgressWind`,
 * `ApplWindowsUpdate/Activate`, `ModDrawWave`, `ModDoSettingsDialog` and the
 * seven setup-management calls (the *schema* they consume is in `schema.ts`;
 * running a modal dialog is not this layer's job). `ModGetOffsToSndHeader/Data`
 * belong with the file parsers in step 5.
 */

import { SoundDocument } from "../document.js";
import { frameSizeFor, getSample, sampleValueLimits } from "../samples.js";
import {
  bytesToUnit,
  unitToBytes,
  type Fixed,
  type UnitCode,
} from "../units.js";
import { getBytesToProcess, resolveSelection } from "../selection.js";
import { silenceOut } from "../edit.js";
import { modResampleSoundBits, modResampleSoundRate } from "./resample.js";
import { ToolboxRandom } from "./rng.js";
import { kModNoError, type ModResult } from "./results.js";

/**
 * `MachineRec` — pre-resolved Gestalt flags, "so the modules don't have to call
 * Gestalt all the time". `[SOURCE: Glue.h]`
 *
 * Kept because modules branch on it (FPU builds especially), and because it is
 * how a Daisy port says "no FPU, take the integer path".
 */
export interface MachineInfo {
  machineRecVers: number;
  hasFPU: boolean;
  hasColorQD: boolean;
  has32BitQD: boolean;
  hasQuickTime: boolean;
  hasSoundManager3: boolean;
  hasSoundInputDevice: boolean;
  hasStereoOutput: boolean;
  hasStereoInput: boolean;
  mixesStereoOutputToMono: boolean;
  hasPlayAndRecord: boolean;
  has16BitSoundIO: boolean;
  hasMultipleChannels: boolean;
}

/**
 * The `doExtend` policy. `[BINARY: Echo-Delay.md]` confirms **2 = "Ask Each
 * Time"** triggers a live `AskDoExtend()` call; anything else is used directly.
 */
export const DoExtend = {
  never: 0,
  always: 1,
  ask: 2,
} as const;

export interface HostOptions {
  /** Seeds `qd.randSeed`. Modules never touch it unless they call initRandSeed. */
  randSeed?: number;
  machine?: Partial<MachineInfo>;
  /** Answers `AskDoExtend()` when the pref is `DoExtend.ask`. */
  askDoExtend?: () => boolean;
  /** Called on every `showProgress`; return true to cancel (only heeded if canCancel). */
  onProgress?: (cur: number, total: number, text: string) => boolean;
  /** How often the host flips `timeToCallProgress`. 1 = every sample. */
  progressInterval?: number;
}

const DEFAULT_MACHINE: MachineInfo = {
  machineRecVers: 1,
  hasFPU: true,
  hasColorQD: true,
  has32BitQD: true,
  hasQuickTime: true,
  hasSoundManager3: true,
  hasSoundInputDevice: true,
  hasStereoOutput: true,
  hasStereoInput: true,
  mixesStereoOutputToMono: false,
  hasPlayAndRecord: true,
  has16BitSoundIO: true,
  hasMultipleChannels: true,
};

/**
 * The default host: everything a module needs, no UI.
 *
 * Settings persistence is in-memory here, keyed by module signature exactly as
 * the app keys its Effect Prefs file. A browser host swaps the two `*Settings`
 * methods for IndexedDB (§12) without the modules noticing.
 */
export class Host {
  readonly machine: MachineInfo;
  readonly random: ToolboxRandom;

  /**
   * `timeToCallProgress` — the host tells the module when to *actually* call
   * `showProgress`. The module polls this flag every sample; the host raises it
   * on its own schedule. It exists so the progress bar's responsiveness is a
   * host preference (Excellent/Good/Fair/Poor, §7.6) rather than each module's
   * guess.
   */
  timeToCallProgress = false;

  /** The pref value. `DoExtend.ask` makes `askDoExtend()` fire per run. */
  doExtend: number = DoExtend.ask;

  private progressText = "";
  private progressOpen = false;
  private sinceLastProgress = 0;
  private readonly progressInterval: number;
  private readonly onProgress: ((cur: number, total: number, text: string) => boolean) | undefined;
  private readonly askDoExtendFn: (() => boolean) | undefined;
  private readonly prefsStore = new Map<string, unknown>();
  private readonly setupsStore = new Map<string, Array<{ name: string; data: unknown }>>();

  constructor(opts: HostOptions = {}) {
    this.machine = { ...DEFAULT_MACHINE, ...opts.machine };
    this.random = new ToolboxRandom(opts.randSeed ?? 1);
    this.progressInterval = opts.progressInterval ?? 1;
    this.onProgress = opts.onProgress;
    this.askDoExtendFn = opts.askDoExtend;
  }

  // --- Progress -----------------------------------------------------------

  /** `ModSetupProgress(modInfo, &timeToCallProgress, textStr)`. */
  setupProgress(text: string): void {
    this.progressText = text;
    this.progressOpen = true;
    this.sinceLastProgress = 0;
    this.timeToCallProgress = true;
  }

  /**
   * `ModShowProgress(cur, total, &timeToCallProgress, canCancel) → OSErr`.
   *
   * **A nonzero return means the user cancelled — and the half-processed audio
   * stays** (§1.2). The module breaks out of its loop and returns; nothing
   * rewinds. Most modules hard-code `canCancel = false` (Amplify, Echo, Delay,
   * Noise, Smooth, Robotize, Downsample Bits), which is why a long Echo is
   * genuinely uninterruptible.
   */
  showProgress(cur: number, total: number, canCancel: boolean): ModResult {
    this.sinceLastProgress = 0;
    this.timeToCallProgress = false;
    const wantsCancel = this.onProgress?.(cur, total, this.progressText) ?? false;
    // Re-arm on the host's own schedule.
    if (++this.sinceLastProgress >= this.progressInterval) this.timeToCallProgress = true;
    if (wantsCancel && canCancel) return 1;
    return kModNoError;
  }

  /** `ModCloseProgress()`. */
  closeProgress(): void {
    this.progressOpen = false;
    this.timeToCallProgress = false;
  }

  get isProgressOpen(): boolean {
    return this.progressOpen;
  }

  // --- Settings persistence -----------------------------------------------

  /**
   * `LoadEffectSettings(signature, &hand)`. Keyed by the module's Pascal-string
   * signature (Gain's is `"\pSampleEffect"`), which is why two modules can share
   * a settings blob if they share a signature.
   */
  loadEffectSettings<S>(signature: string): S | null {
    return (this.prefsStore.get(signature) as S) ?? null;
  }

  /** `SaveEffectSettings(signature, &hand)`. */
  saveEffectSettings<S>(signature: string, settings: S): ModResult {
    this.prefsStore.set(signature, settings);
    return kModNoError;
  }

  // --- Named setups (host-managed, §7.5) ----------------------------------

  getSetupsNumber(signature: string): number {
    return this.setupsStore.get(signature)?.length ?? 0;
  }

  getSetup<S>(signature: string, num: number): S | null {
    return (this.setupsStore.get(signature)?.[num]?.data as S) ?? null;
  }

  getSetupName(signature: string, num: number): string | null {
    return this.setupsStore.get(signature)?.[num]?.name ?? null;
  }

  setSetup<S>(signature: string, num: number, data: S, name: string): ModResult {
    const list = this.setupsStore.get(signature) ?? [];
    if (num >= 0 && num < list.length) list[num] = { name, data };
    else list.push({ name, data });
    this.setupsStore.set(signature, list);
    return kModNoError;
  }

  deleteSetup(signature: string, num: number): ModResult {
    const list = this.setupsStore.get(signature);
    if (list && num >= 0 && num < list.length) list.splice(num, 1);
    return kModNoError;
  }

  // --- Environment --------------------------------------------------------

  /** `GetMachineInfo(&MachinePtr)`. */
  getMachineInfo(): MachineInfo {
    return this.machine;
  }

  /**
   * `AskDoExtend() → short`.
   *
   * Only called when the pref is `DoExtend.ask` — the module checks first
   * (`Echo-Delay.md` step 5). "Ask each time" is one of the app's
   * non-negotiables (§12), so the default when no answerer is wired up is
   * *don't extend*: silently growing someone's buffer is the rude option.
   */
  askDoExtend(): boolean {
    if (this.doExtend === DoExtend.ask) return this.askDoExtendFn?.() ?? false;
    return this.doExtend === DoExtend.always;
  }

  /** `ModInitRandSeed()`. */
  initRandSeed(): void {
    this.random.initSeed();
  }

  // --- Unit conversion ----------------------------------------------------

  /** `ModConvertToUnits(modInfo, destStr, srcVal, newUnits)`. */
  convertToUnits(doc: SoundDocument, bytes: number, units: UnitCode): number {
    return bytesToUnit(bytes, units, doc.rate, doc.bps);
  }

  /** `ModConvertToBytes(modInfo, srcStr, &destVal, newUnits)`. */
  convertToBytes(doc: SoundDocument, value: number, units: UnitCode): number {
    return unitToBytes(value, units, doc.rate, doc.bps);
  }

  // --- Selection queries --------------------------------------------------

  /** `GetBytesToProcess(modInfo) → long`. */
  getBytesToProcess(doc: SoundDocument): number {
    return getBytesToProcess(doc);
  }

  /** `ModMaxChSize` — max over ALL channels. */
  maxChSize(doc: SoundDocument): number {
    return doc.maxChSize();
  }

  /** `ModMaxRelChSize` — max over the SELECTED channels. */
  maxRelChSize(doc: SoundDocument): number {
    return doc.maxRelChSize();
  }

  /**
   * `ModGetPeak(modData, &peak, &pos)`.
   *
   * Note this is *not* Select Peak's metric: Select Peak sums across channels
   * (`Select-Silence-Peak.md`), whereas this service reports the largest single
   * |sample| and where it is. Flanger's envelope follower uses it over 1024-sample
   * windows. `[SOURCE: Glue.h]` for the signature; the per-channel-max reading is
   * `[DOC]` — the app-level routine was not disassembled.
   */
  getPeak(doc: SoundDocument): { peak: number; pos: number } {
    const frameSize = frameSizeFor(doc.bps);
    const { start, end } = resolveSelection(doc);
    let peak = 0;
    let pos = 0;
    for (const i of doc.selectedChannelIndices()) {
      const buf = doc.channels[i]!.data;
      const hi = Math.min(end, buf.length - (buf.length % frameSize));
      for (let p = start; p + frameSize <= hi; p += frameSize) {
        const v = Math.abs(getSample(buf, p, doc.bps));
        if (v > peak) {
          peak = v;
          pos = p;
        }
      }
    }
    return { peak, pos };
  }

  /** `ModGetSampleValueLimits(bps, &min, &max)`. */
  getSampleValueLimits(bps: number): { min: number; max: number } {
    return sampleValueLimits(bps);
  }

  // --- Document restructuring ---------------------------------------------

  /** `ModAddNewChannel(modData, where)` — 1-based insert position. */
  addNewChannel(doc: SoundDocument, where?: number): ModResult {
    doc.addChannel(where);
    return kModNoError;
  }

  /** `ModDelChannel(modData, chan)` — 1-based. */
  delChannel(doc: SoundDocument, chan: number): ModResult {
    return doc.delChannel(chan) ? kModNoError : 1;
  }

  /** `ModGetChannelPan(chAttrs, channel, leftOne)` — 0-based channel. */
  getChannelPan(doc: SoundDocument, channel: number, leftOne: boolean): boolean {
    const ch = doc.channels[channel];
    if (!ch) return false;
    return leftOne ? ch.outLeft : ch.outRight;
  }

  /** `ModSetChannelPan(chAttrs, channel, leftPan, rightPan)`. */
  setChannelPan(
    doc: SoundDocument,
    channel: number,
    leftPan: boolean,
    rightPan: boolean,
  ): void {
    const ch = doc.channels[channel];
    if (!ch) return;
    ch.outLeft = leftPan;
    ch.outRight = rightPan;
  }

  /** `ModSilenceOut(modData, chSt, chEnd, st, end)` — 1-based channels. */
  silenceOut(
    doc: SoundDocument,
    chSt: number,
    chEnd: number,
    st: number,
    end: number,
  ): ModResult {
    silenceOut(doc, chSt, chEnd, st, end);
    return kModNoError;
  }

  /** `ModResampleSoundBits(modData, newBps)`. */
  resampleSoundBits(doc: SoundDocument, newBps: number): ModResult {
    return modResampleSoundBits(doc, newBps);
  }

  /** `ModResampleSoundRate(modData, newRate:Fixed)`. */
  resampleSoundRate(doc: SoundDocument, newRate: Fixed): ModResult {
    return modResampleSoundRate(doc, newRate);
  }

  /**
   * Documents modules spawned during this run, in creation order.
   *
   * `effect()` returns an `OSErr`, not a document, so a module that renders to a
   * "New Document" has no way to hand it back — in the original it calls
   * `ModNewDocument(&modData)` and the *host* owns the resulting window. This
   * list is that ownership: the host opens whatever ends up here. Quick Mix and
   * Difference are the modules that use it.
   */
  readonly spawnedDocuments: SoundDocument[] = [];

  /**
   * `ModNewDocument(&modData)` — spawn an empty document matching this one's
   * format, and record it for the host to open.
   */
  newDocument(doc: SoundDocument): SoundDocument {
    const fresh = new SoundDocument({
      name: "untitled sound",
      rate: doc.rate,
      bps: doc.bps,
      baseFreq: doc.baseFreq,
      numChans: 0,
    });
    this.spawnedDocuments.push(fresh);
    return fresh;
  }

  /** `ModShowError(modInfo, errNum, withCancel)`. Collected, not drawn. */
  readonly errors: Array<{ errNum: number; withCancel: boolean }> = [];

  showError(errNum: number, withCancel = false): void {
    this.errors.push({ errNum, withCancel });
  }
}
