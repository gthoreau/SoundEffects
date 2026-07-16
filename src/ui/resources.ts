/**
 * The resource fork, typed — SPEC §15 step 7.
 *
 * Steps 2–6 had to *port* things: C source transliterated, 68k kernels
 * recovered byte by byte. The chrome needs none of that, because the chrome was
 * already data. Menus, windows, dialogs, controls and the whole first-person
 * string corpus are declarative resources; `tools/uidump.py` reads them out and
 * this module puts types on them. **Nothing here is authored.** If a rect looks
 * wrong, the fix is in the original's designer's hands, not ours.
 *
 * The fork itself is gitignored (it only survives in HFS resource forks), so
 * `resources/*.json` is the committed artifact and `npm run resources:check`
 * proves it still matches the fork.
 */

import windowsJson from "./resources/windows.json" with { type: "json" };
import dialogsJson from "./resources/dialogs.json" with { type: "json" };
import menusJson from "./resources/menus.json" with { type: "json" };
import controlsJson from "./resources/controls.json" with { type: "json" };
import stringsJson from "./resources/strings.json" with { type: "json" };

/** QuickDraw's rect, and its order: top, left, bottom, right. */
export interface Rect {
  top: number;
  left: number;
  bottom: number;
  right: number;
}

export function rectWidth(r: Rect): number {
  return r.right - r.left;
}

export function rectHeight(r: Rect): number {
  return r.bottom - r.top;
}

/** `WIND` — a window template. `[RES]` */
export interface WindTemplate {
  rect: Rect;
  procID: number;
  visible: boolean;
  goAway: boolean;
  refCon: number;
  title: string;
  positioning?: number;
}

/** `DLOG` — a WIND plus the `DITL` that fills it. `[RES]` */
export interface DlogTemplate extends WindTemplate {
  itemsID: number;
}

/** `ALRT` — rect + item list + the packed four-stage word. `[RES]` */
export interface AlrtTemplate {
  rect: Rect;
  itemsID: number;
  stages: number;
  positioning?: number;
}

export type DitlKind =
  | "userItem"
  | "helpItem"
  | "button"
  | "checkBox"
  | "radioButton"
  | "resControl"
  | "staticText"
  | "editText"
  | "icon"
  | "picture";

/** One `DITL` item. `id` is 1-based — the Toolbox numbers items from 1. */
export interface DitlItem {
  id: number;
  kind: DitlKind | string;
  /** Resource-level "reports clicks". Static text ships disabled as a matter of course. */
  enabled: boolean;
  rect: Rect;
  text?: string;
  /** For `resControl` (a CNTL id), `icon` (ICON/cicn) and `picture` (PICT). */
  resID?: number;
  data?: number[];
}

/** One `MENU` item. `key` is the ⌘ equivalent; see {@link hierarchicalSubmenuID}. */
export interface MenuItem {
  index: number;
  text: string;
  icon: number;
  /** A character, or a raw byte when it is not printable. `0` = no shortcut. */
  key: string | number;
  mark: string | number;
  style: number;
  enabled: boolean;
}

export interface MenuTemplate {
  menuID: number;
  width: number;
  height: number;
  procID: number;
  enableFlags: number;
  enabled: boolean;
  title: string;
  items: MenuItem[];
}

/** `CNTL` — a control template. `[RES]` */
export interface CntlTemplate {
  rect: Rect;
  value: number;
  visible: boolean;
  max: number;
  min: number;
  procID: number;
  refCon: number;
  title: string;
}

type IdMap<T> = Record<string, T>;

export const WIND = windowsJson.WIND as unknown as IdMap<WindTemplate>;
export const DLOG = windowsJson.DLOG as unknown as IdMap<DlogTemplate>;
export const ALRT = windowsJson.ALRT as unknown as IdMap<AlrtTemplate>;
export const DITL = dialogsJson.DITL as unknown as IdMap<DitlItem[]>;
export const MENU = menusJson.MENU as unknown as IdMap<MenuTemplate>;
export const MBAR = menusJson.MBAR as unknown as IdMap<number[]>;
export const CNTL = controlsJson.CNTL as unknown as IdMap<CntlTemplate>;
export const STR_LIST = stringsJson["STR#"] as unknown as IdMap<string[]>;
export const STR = stringsJson.STR as unknown as IdMap<string>;

/**
 * A missing resource is not a soft failure. The original's own word for it is
 * "I couldn't find my own resources!" (STR# 128) and it means the app is broken,
 * not that a default should be invented.
 */
function need<T>(map: IdMap<T>, id: number, type: string): T {
  const r = map[String(id)];
  if (r === undefined) throw new Error(`I couldn't find my own resources! (${type} ${id})`);
  return r;
}

export const getWind = (id: number): WindTemplate => need(WIND, id, "WIND");
export const getDlog = (id: number): DlogTemplate => need(DLOG, id, "DLOG");
export const getAlrt = (id: number): AlrtTemplate => need(ALRT, id, "ALRT");
export const getDitl = (id: number): DitlItem[] => need(DITL, id, "DITL");
export const getMenu = (id: number): MenuTemplate => need(MENU, id, "MENU");
export const getMbar = (id: number): number[] => need(MBAR, id, "MBAR");
export const getCntl = (id: number): CntlTemplate => need(CNTL, id, "CNTL");

/**
 * `STR#` indices are **1-based** (`GetIndString`'s convention), and the app's
 * error voice depends on it: §7.7 indexes a module's `STR# 1000` by the absolute
 * value of its result code, and there is no code 0.
 */
export function getIndString(listID: number, index: number): string {
  const list = need(STR_LIST, listID, "STR#");
  const s = list[index - 1];
  if (s === undefined) {
    throw new Error(`I couldn't find my own resources! (STR# ${listID} index ${index})`);
  }
  return s;
}

export const getStr = (id: number): string => need(STR, id, "STR");

/** `hMenuCmd` — a key byte of 0x1B means "not a shortcut: I have a submenu". */
export const hMenuCmd = 0x1b;

/**
 * The submenu id of a hierarchical item, or `null` if it is an ordinary one.
 *
 * When the key byte is `hMenuCmd` the *mark* byte stops being a checkmark and
 * becomes the submenu's id. Three items in the menu bar do this: File's
 * Preferences (→ MENU 140), Edit's Zoom (→ 141) and Sound's Volume (→ 142).
 */
export function hierarchicalSubmenuID(item: MenuItem): number | null {
  if (item.key !== hMenuCmd) return null;
  return typeof item.mark === "string" ? item.mark.charCodeAt(0) : item.mark;
}

/** A `-` item is the Toolbox's separator. It ships disabled, which is the tell. */
export function isSeparator(item: MenuItem): boolean {
  return item.text === "-";
}

/** The ⌘ equivalent, or `null` for none / for a hierarchical item. */
export function commandKey(item: MenuItem): string | null {
  if (typeof item.key !== "string") return null;
  return item.key;
}

/** Resource ids the shell reaches for by name rather than by number. */
export const Res = {
  mbar: 128,
  menu: {
    apple: 128,
    file: 129,
    edit: 130,
    selection: 131,
    sound: 132,
    effects: 133,
    special: 134,
    windows: 135,
    prefsPages: 140,
    zoom: 141,
    volume: 142,
    /** The 6-unit popup of §7.5 — samples/bytes/seconds/ms/meters/feet. */
    units: 157,
    /** The named-setups popup of §7.5. */
    setups: 160,
  },
  wind: {
    document: 128,
    about: 129,
    progress: 130,
    effectInfo: 131,
    mixer: 132,
    record: 133,
    level: 134,
    chunkList: 135,
    spectrum: 136,
    register: 137,
    playClipboard: 138,
  },
  /** The 66-string first-person error corpus of §11. */
  errors: 128,
  /** Cut/Copy/Paste/Clear ⇄ …Channel relabelling (§4). */
  editLabels: 136,
} as const;
