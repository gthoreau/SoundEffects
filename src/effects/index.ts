/**
 * The Effects Folder — discovery and menu grouping. SPEC §7.1. `[RES]`
 *
 * At launch the app scans the Effects Folder; each module contributes one menu
 * item, and a module with a `Grup` string goes into that named submenu. Here the
 * "folder" is this array — but the grouping data is the recovered one, and the
 * menu templating ("<Name> Options…", "About <Name>…") is `[RES: STR# 140]`.
 */

import type { EffectGroup, EffectModule } from "../runtime/module.js";

import { Amplify } from "./amplify.js";
import { Smooth } from "./smooth.js";
import { Robotize } from "./robotize.js";
import { Noise } from "./noise.js";
import { Dither } from "./dither.js";
import { WeirdTempo } from "./weird-tempo.js";
import { HighPassIIR, LowPassIIR, Shelving } from "./iir.js";
import { Echo, Delay } from "./echo-delay.js";
import { Flanger } from "./flanger.js";
import { BandpassFIR, BandstopFIR, HighPassFIR, LowPassFIR } from "./fir.js";
import {
  DrawLine,
  FadeIn,
  FadeOut,
  Invert,
  Normalize,
  Reverse,
  Silence,
} from "./simple.js";
import { DownsampleBits, ResampleRate, UpsampleBits } from "./resampling.js";
import { Difference, SelectionToChannel, Sum } from "./channels.js";
import { GranularSynthesis } from "./granular.js";
import { SampleHose } from "./sample-hose.js";
import { IterativeCrossfade } from "./iterative-crossfade.js";
import { QuickMix } from "./quick-mix.js";
import { Replicate } from "./replicate.js";
import { WaveformGenerator } from "./waveform.js";
import { ZeroCrosser } from "./zero-crosser.js";
import { Keyboard } from "./keyboard.js";

export * from "./amplify.js";
export * from "./smooth.js";
export * from "./robotize.js";
export * from "./noise.js";
export * from "./dither.js";
export * from "./weird-tempo.js";
export * from "./iir.js";
export * from "./echo-delay.js";
export * from "./flanger.js";
export * from "./fir.js";
export * from "./simple.js";
export * from "./resampling.js";
export * from "./channels.js";
export * from "./norris-shared.js";
export * from "./granular.js";
export * from "./sample-hose.js";
export * from "./iterative-crossfade.js";
export * from "./quick-mix.js";
export * from "./replicate.js";
export * from "./waveform.js";
export * from "./zero-crosser.js";
export * from "./keyboard.js";

/**
 * The Effects Folder — **all 37 shipped modules**.
 *
 * (§8's 38th entry is the SDK's Gain/EmptyEffect templates, which never shipped.
 * Gain *is* Amplify, and it is here.)
 *
 * 36 of these are buffer effects with an `effect()` kernel. **Keyboard** (§8 #18)
 * is the odd one: no kernel, because it is a sampler made of a rate multiplier.
 * Its `effect()` is the Change Pitch commit — a header edit that touches no audio
 * — and its auditioning lives in `audio/keyboard-voice.ts`. Only its window is
 * outstanding (step 7).
 */
// eslint-disable-next-line @typescript-eslint/no-explicit-any
export const EFFECTS: ReadonlyArray<EffectModule<any>> = [
  // Top level
  Amplify,
  Dither,
  FadeIn,
  FadeOut,
  Keyboard,
  Noise,
  Normalize,
  QuickMix,
  Replicate,
  Reverse,
  Robotize,
  SelectionToChannel,
  Silence,
  Smooth,
  WaveformGenerator,
  WeirdTempo,
  // Echoes & Reverbs
  Delay,
  Echo,
  Flanger,
  // FIR/IIR Filters
  BandpassFIR,
  BandstopFIR,
  HighPassFIR,
  HighPassIIR,
  LowPassFIR,
  LowPassIIR,
  Shelving,
  // Resampling
  DownsampleBits,
  ResampleRate,
  UpsampleBits,
  // Misc
  Difference,
  DrawLine,
  Invert,
  Sum,
  // Mike's Modules
  GranularSynthesis,
  IterativeCrossfade,
  SampleHose,
  // Selection
  ZeroCrosser,
];

/** The submenu order as recovered. Modules with no group sit at top level. */
export const GROUP_ORDER: readonly EffectGroup[] = [
  "Echoes & Reverbs",
  "FIR/IIR Filters",
  "Resampling",
  "Misc",
  "Mike's Modules",
  "Selection",
];

export interface EffectMenu {
  topLevel: EffectModule<never>[];
  groups: Array<{ name: EffectGroup; modules: EffectModule<never>[] }>;
}

/** Build the Effects menu: grouped submenus plus top-level items, each A–Z. */
export function buildEffectMenu(
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  modules: ReadonlyArray<EffectModule<any>> = EFFECTS,
): EffectMenu {
  const byName = (a: EffectModule<never>, b: EffectModule<never>) =>
    a.name.localeCompare(b.name);
  const topLevel = modules.filter((m) => !m.group) as EffectModule<never>[];
  const groups = GROUP_ORDER.map((name) => ({
    name,
    modules: (modules.filter((m) => m.group === name) as EffectModule<never>[]).sort(byName),
  })).filter((g) => g.modules.length > 0);
  return { topLevel: topLevel.sort(byName), groups };
}

/** "<Name> Options…" / "About <Name>…" — the menu templating. `[RES: STR# 140]` */
export function optionsItemTitle(module: EffectModule<never>): string {
  return `${module.name} Options…`;
}

export function aboutItemTitle(module: EffectModule<never>): string {
  return `About ${module.name}…`;
}

/** Look a module up by signature, as the prefs store keys them. */
export function findBySignature(
  signature: string,
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  modules: ReadonlyArray<EffectModule<any>> = EFFECTS,
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
): EffectModule<any> | undefined {
  return modules.find((m) => m.signature === signature);
}
