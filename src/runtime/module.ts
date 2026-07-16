/**
 * The module contract and message dispatch — SPEC §7.2, `[SOURCE: Glue.h/Glue.c]`.
 *
 * An effect file is a code resource whose `main` takes one of five messages;
 * in practice every module links the SDK's `Glue.c`, which decodes the message
 * and calls one of the three routines the author actually writes (`effect`,
 * `settings`, `about`). `dispatch()` below *is* that `Glue.c`.
 *
 * We keep the message layer rather than exposing `effect()` directly because the
 * message is what distinguishes "run with last settings" (`Mrun`, i.e. the
 * option-key / Redo Effect path) from "ask, then run" (`Msar`) — a distinction
 * with no other home, and one the menu depends on.
 */

import type { SoundDocument } from "../document.js";
import type { Host } from "./host.js";
import type { SettingsSchema } from "./schema.js";
import {
  kModHasNoSettings,
  kModNoError,
  kModUnknownMessage,
  type ModResult,
} from "./results.js";

/** `kHeadersVersion` — the ABI version, answered to `Mhvr`. */
export const kHeadersVersion = 1;

/** The five messages, as their original four-char codes. `[SOURCE: Glue.h]` */
export const Msg = {
  /** Run the effect without showing any settings dialog. */
  run: "Mrun",
  /** Let the user change settings, if there are any. */
  settings: "Mset",
  /** Let the user change settings, then run. */
  settingsRun: "Msar",
  /** Store kHeadersVersion and return noErr. */
  headersVers: "Mhvr",
  /** Do whatever you want to describe your module. */
  about: "Minf",
} as const;

export type ModMessage = (typeof Msg)[keyof typeof Msg];

/** §7.1 — the recovered Effects Folder grouping. A module with no `Grup` is top level. */
export type EffectGroup =
  | "Echoes & Reverbs"
  | "FIR/IIR Filters"
  | "Resampling"
  | "Misc"
  | "Mike's Modules"
  | "Selection";

/**
 * One effect module. Mirrors what a real `EFCT` resource carries: its code, its
 * settings struct, its `STR# 1000` voice, its `Grup`, its About text.
 *
 * `S` is the module's `ModSettingsRec` — an opaque struct the author declares
 * and the host persists by `signature` without ever looking inside.
 */
export interface EffectModule<S = Record<string, never>> {
  /** Menu name, e.g. "Amplify". Templated as "<Name> Options…" / "About <Name>…". */
  name: string;
  /** The Pascal-string settings key, e.g. Gain's "SampleEffect". */
  signature: string;
  /** The `Grup` resource. Omit for a top-level menu item. */
  group?: EffectGroup;
  version?: string;
  author?: string;
  /** The About text (TEXT 1000). Shown for `Minf`. */
  about?: string;
  /** `STR# 1000` — indexed by |result| for the module's own error voice (§7.7). */
  strings?: readonly string[];
  /** `STR ' 128` — the progress window text, e.g. "Amplifying by ". */
  progressText?: string;

  /** The settings struct's defaults, as baked into the DITL. */
  defaults?(): S;
  /**
   * The declarative dialog (§7.5). Omit if the module has no parameters — then
   * `settings` answers `kModHasNoSettings` ("Sorry, man. I haven't got any
   * options.") — or if the module draws its own dialog, as Dither does.
   */
  schema?(): SettingsSchema;

  /** The effect itself. Mirrors `effect(modInfo, glue, &prefs)`. */
  effect(doc: SoundDocument, host: Host, prefs: S): ModResult;

  /**
   * Optional validation/coercion applied when settings are committed, mirroring
   * the `settings()` routine's own clamping (Gain clamps gain ≤ 32767).
   */
  validate?(prefs: S): S;
}

/**
 * Asks the user for settings. Headless by construction: the runtime has no
 * dialogs, so a host supplies one. Return null to cancel.
 */
export type SettingsResolver = <S>(
  module: EffectModule<S>,
  doc: SoundDocument,
  current: S,
  schema: SettingsSchema | null,
) => S | null;

/**
 * `GetModSettings` — load-or-default. `[SOURCE: Glue.h]`
 *
 * The persisted blob is keyed by signature, so "run with last settings" needs no
 * special path: it is just `Mrun` finding what `Mset` left behind.
 */
export function getModSettings<S>(module: EffectModule<S>, host: Host): S {
  const stored = host.loadEffectSettings<S>(module.signature);
  if (stored !== null) return stored;
  return module.defaults ? module.defaults() : ({} as S);
}

/**
 * `main(message, modInfo, glue68k, gluePPC)` — the SDK's `Glue.c`, transcribed.
 *
 * The prefs lifecycle from §7.2 is worth stating because it looks odd until you
 * see why: `settings` loads-or-defaults, saves, and **leaves the handle
 * allocated** so an immediately-following `effect` can consume it; `effect`
 * disposes it. We have a garbage collector and no handles, so "dispose" is
 * meaningless — but the *observable* half of that contract is real and preserved:
 * `Msar` runs the effect with exactly the settings the dialog just produced,
 * without a reload, and those settings are already persisted by then.
 */
export function dispatch<S>(
  module: EffectModule<S>,
  message: ModMessage,
  doc: SoundDocument,
  host: Host,
  resolver?: SettingsResolver,
): ModResult {
  switch (message) {
    case Msg.headersVers:
      return kHeadersVersion;

    case Msg.about:
      // The host shows module.about; nothing to compute.
      return kModNoError;

    case Msg.run: {
      const prefs = getModSettings(module, host);
      return module.effect(doc, host, prefs);
    }

    case Msg.settings: {
      const result = runSettings(module, doc, host, resolver);
      return typeof result === "number" ? result : kModNoError;
    }

    case Msg.settingsRun: {
      const result = runSettings(module, doc, host, resolver);
      if (typeof result === "number") {
        // kModHasNoSettings means "no dialog to show" — run anyway, which is how
        // a no-parameter module behaves on its menu item.
        if (result !== kModHasNoSettings) return result;
        return module.effect(doc, host, getModSettings(module, host));
      }
      return module.effect(doc, host, result);
    }

    default:
      return kModUnknownMessage;
  }
}

/**
 * The `settings` routine. Returns the committed settings, or a result code when
 * there was nothing to ask (`kModHasNoSettings`) or the user cancelled.
 */
function runSettings<S>(
  module: EffectModule<S>,
  doc: SoundDocument,
  host: Host,
  resolver?: SettingsResolver,
): S | ModResult {
  const schema = module.schema?.() ?? null;
  if (!schema || schema.length === 0) return kModHasNoSettings;

  const current = getModSettings(module, host);
  if (!resolver) return kModHasNoSettings;

  const chosen = resolver(module, doc, current, schema);
  if (chosen === null) return -128; // kModCancel: the user backed out

  const validated = module.validate ? module.validate(chosen) : chosen;
  host.saveEffectSettings(module.signature, validated);
  return validated;
}

/**
 * Convenience for callers (and tests) that already know the settings: run the
 * module directly, as `Mrun` would with a persisted blob.
 */
export function runEffect<S>(
  module: EffectModule<S>,
  doc: SoundDocument,
  host: Host,
  prefs?: Partial<S>,
): ModResult {
  const base = module.defaults ? module.defaults() : ({} as S);
  const merged = { ...base, ...prefs } as S;
  const final = module.validate ? module.validate(merged) : merged;
  return module.effect(doc, host, final);
}
