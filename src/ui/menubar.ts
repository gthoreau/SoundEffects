/**
 * The menu bar — SPEC §4, built from `MBAR 128` → `MENU 128…135`.
 *
 * Not one menu title is written here. `MBAR 128` lists the eight menus, each
 * `MENU` carries its own items, keys and marks, and three items are hierarchical
 * (File's Preferences → MENU 140, Edit's Zoom → 141, Sound's Volume → 142). The
 * §4 reference in the SPEC and this file are two readings of the same bytes; if
 * they disagree, the bytes win.
 *
 * **The Apple menu's title is `\x14`** — character 20 in the Chicago font is the
 *  glyph. No modern font has it there, so it is the one string the shell
 * substitutes, and it substitutes a  rather than reproducing a mojibake DC4.
 *
 * §1.2's contract shows up as one line of code: Undo is disabled in the
 * resource (`enableFlags` bit 1 is clear on MENU 130 item 1) and this renderer
 * simply obeys that. Nothing special-cases it. The religion is in the data.
 */

import { el } from "./dom.js";
import {
  type MenuItem,
  type MenuTemplate,
  Res,
  commandKey,
  getMbar,
  getMenu,
  hierarchicalSubmenuID,
  isSeparator,
} from "./resources.js";

/** Chicago's  at 0x14. */
export const APPLE_GLYPH = "";

export function menuTitle(m: MenuTemplate): string {
  return m.title === "\x14" ? APPLE_GLYPH : m.title;
}

/** A command is addressed the way the Toolbox addressed it: menu id + item index. */
export interface MenuRef {
  menuID: number;
  item: number;
}

export function refKey(menuID: number, item: number): string {
  return `${menuID}:${item}`;
}

/** What the shell decides about an item at pull-down time. */
export interface MenuItemState {
  enabled?: boolean;
  /** A checkmark, or a bullet for a radio-style group (Volume uses one). */
  mark?: string | null;
  /** Dynamic relabelling — Cut ⇄ Cut Channel (§4, STR# 136). */
  text?: string;
}

export interface MenuBarOptions {
  /** Called for every item when a menu opens. Return `{}` to accept the resource's state. */
  stateFor?: (ref: MenuRef, item: MenuItem) => MenuItemState;
  onCommand: (ref: MenuRef, item: MenuItem, mods: Modifiers) => void;
  /** Items appended at runtime: the Effects menu (§7.1) and the Windows list (§4). */
  dynamicItems?: (menuID: number) => MenuItem[];
}

/**
 * §7.1's modifier contract, which the Effects menu depends on: plain = show
 * settings then run, option = run with last settings, shift = show the about
 * box, option+shift = show settings but don't run.
 */
export interface Modifiers {
  option: boolean;
  shift: boolean;
  command: boolean;
}

export function modifiersOf(e: MouseEvent | KeyboardEvent): Modifiers {
  return { option: e.altKey, shift: e.shiftKey, command: e.metaKey || e.ctrlKey };
}

export class MenuBar {
  readonly element: HTMLElement;
  private opts: MenuBarOptions;
  private menus: MenuTemplate[];
  private openMenu: { title: HTMLElement; panel: HTMLElement } | null = null;
  /** ⌘-key → the item it fires, built once from the resources. */
  private shortcuts = new Map<string, { ref: MenuRef; item: MenuItem }>();

  constructor(opts: MenuBarOptions) {
    this.opts = opts;
    this.menus = getMbar(Res.mbar).map(getMenu);
    this.element = el("div", { id: "sfx-menubar" });

    for (const m of this.menus) this.element.append(this.buildTitle(m));
    this.indexShortcuts();

    // A click anywhere else dismisses the open menu.
    //
    // The panel is appended to <body>, not to the bar — it has to be, or the bar's
    // own overflow would clip it — so "outside" must exclude the *panel* as well
    // as the bar. Miss that and mousedown tears the panel down before the item's
    // click can fire, which silently kills every command in the app.
    window.addEventListener("mousedown", (e) => {
      if (!this.openMenu) return;
      const t = e.target as Node;
      if (this.element.contains(t) || this.openMenu.panel.contains(t)) return;
      this.close();
    });
    window.addEventListener("keydown", (e) => this.onKeyDown(e));
  }

  private indexShortcuts(): void {
    const walk = (m: MenuTemplate) => {
      for (const item of m.items) {
        const sub = hierarchicalSubmenuID(item);
        if (sub !== null) {
          walk(getMenu(sub));
          continue;
        }
        const key = commandKey(item);
        if (key) {
          this.shortcuts.set(key.toUpperCase(), {
            ref: { menuID: m.menuID, item: item.index },
            item,
          });
        }
      }
    };
    for (const m of this.menus) walk(m);
  }

  private onKeyDown(e: KeyboardEvent): void {
    if (!(e.metaKey || e.ctrlKey)) return;
    const hit = this.shortcuts.get(e.key.toUpperCase());
    if (!hit) return;
    const state = this.stateOf(hit.ref, hit.item);
    // A disabled item's shortcut does nothing — and ⌘Z must still be swallowed,
    // or the browser's own undo would answer for it. §1.2 is not negotiable.
    e.preventDefault();
    if (state.enabled === false) return;
    this.opts.onCommand(hit.ref, hit.item, modifiersOf(e));
  }

  private stateOf(ref: MenuRef, item: MenuItem): MenuItemState {
    const override = this.opts.stateFor?.(ref, item) ?? {};
    return { enabled: item.enabled, ...override };
  }

  private buildTitle(m: MenuTemplate): HTMLElement {
    const title = el("div", {
      class: "sfx-menu-title",
      text: menuTitle(m),
      "data-menu-id": m.menuID,
      "aria-expanded": "false",
      "aria-disabled": String(!m.enabled),
    });
    title.addEventListener("mousedown", (e) => {
      e.preventDefault();
      e.stopPropagation();
      if (this.openMenu?.title === title) this.close();
      else this.pullDown(m, title);
    });
    // With a menu already down, moving across the bar switches to that menu —
    // System 7's menus tracked the mouse the whole time it was held.
    title.addEventListener("mouseenter", () => {
      if (this.openMenu && this.openMenu.title !== title) this.pullDown(m, title);
    });
    return title;
  }

  private pullDown(m: MenuTemplate, title: HTMLElement): void {
    this.close();
    const panel = this.buildPanel(m);
    panel.style.left = `${title.offsetLeft}px`;
    title.setAttribute("aria-expanded", "true");
    document.body.append(panel);
    this.openMenu = { title, panel };
  }

  private buildPanel(m: MenuTemplate): HTMLElement {
    const panel = el("div", { class: "sfx-menu", "data-menu-id": m.menuID });
    const items = [...m.items, ...(this.opts.dynamicItems?.(m.menuID) ?? [])];

    for (const item of items) {
      if (isSeparator(item)) {
        panel.append(el("div", { class: "sfx-menu-separator" }));
        continue;
      }
      panel.append(this.buildItem(m, item));
    }
    return panel;
  }

  private buildItem(m: MenuTemplate, item: MenuItem): HTMLElement {
    const ref: MenuRef = { menuID: m.menuID, item: item.index };
    const state = this.stateOf(ref, item);
    const enabled = state.enabled !== false;
    const submenuID = hierarchicalSubmenuID(item);

    const row = el("div", {
      class: "sfx-menu-item",
      "aria-disabled": String(!enabled),
      "data-item": item.index,
    });
    row.append(el("span", { class: "sfx-menu-mark", text: state.mark ?? "" }));
    row.append(el("span", { text: state.text ?? item.text }));

    if (submenuID !== null) {
      row.append(el("span", { class: "sfx-menu-key", text: "▶" }));
      const sub = this.buildSubmenu(getMenu(submenuID), enabled);
      row.append(sub);
    } else {
      const key = commandKey(item);
      if (key) row.append(el("span", { class: "sfx-menu-key", text: `⌘${key}` }));
      if (enabled) {
        row.addEventListener("click", (e) => {
          this.close();
          this.opts.onCommand(ref, item, modifiersOf(e));
        });
      }
    }
    return row;
  }

  private buildSubmenu(m: MenuTemplate, parentEnabled: boolean): HTMLElement {
    const panel = el("div", { class: "sfx-menu sfx-submenu", "data-menu-id": m.menuID });
    for (const item of m.items) {
      if (isSeparator(item)) {
        panel.append(el("div", { class: "sfx-menu-separator" }));
        continue;
      }
      const ref: MenuRef = { menuID: m.menuID, item: item.index };
      const state = this.stateOf(ref, item);
      const enabled = parentEnabled && state.enabled !== false;
      const row = el("div", {
        class: "sfx-menu-item",
        "aria-disabled": String(!enabled),
        "data-item": item.index,
      });
      row.append(el("span", { class: "sfx-menu-mark", text: state.mark ?? "" }));
      row.append(el("span", { text: state.text ?? item.text }));
      const key = commandKey(item);
      if (key) row.append(el("span", { class: "sfx-menu-key", text: `⌘${key}` }));
      if (enabled) {
        row.addEventListener("click", (e) => {
          e.stopPropagation();
          this.close();
          this.opts.onCommand(ref, item, modifiersOf(e));
        });
      }
      panel.append(row);
    }
    return panel;
  }

  close(): void {
    if (!this.openMenu) return;
    this.openMenu.title.setAttribute("aria-expanded", "false");
    this.openMenu.panel.remove();
    this.openMenu = null;
  }
}
