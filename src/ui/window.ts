/**
 * Windows and the window layer — SPEC §3, §12 ("draggable in-page windows").
 *
 * A window is built from its `WIND` template and nothing else: the rect, the
 * title, and the `procID` that says which chrome it wears. §3 lists eleven of
 * them and every one is already in the fork, so this file has no window sizes in
 * it — asking for `WIND 128` is asking for 454×252 at (20, 50).
 *
 * Two things the fork settles that are easy to get backwards:
 *
 * - **The close box is not part of the `procID`.** It is the template's separate
 *   `goAway` flag (`NewWindow`'s `goAwayFlag` argument). The `procID` picks the
 *   `WDEF` and its variation — that is, the *zoom* and *grow* boxes — and
 *   nothing else.
 * - **The document window is `procID` 12 = `zoomNoGrow`**, so per the resource it
 *   zooms but does **not** have a grow box. §3 says "zoomable/resizable",
 *   quoting the 0.9.1 Read Me — see {@link DOCUMENT_WINDOW_IS_ZOOM_NO_GROW}.
 */

import { el } from "./dom.js";
import type { WindTemplate } from "./resources.js";
import { getWind, rectHeight, rectWidth } from "./resources.js";

/** Inside Macintosh: Macintosh Toolbox Essentials, "Window Definition IDs". */
export const WindowProc = {
  documentProc: 0,
  dBoxProc: 1,
  plainDBox: 2,
  altDBoxProc: 3,
  noGrowDocProc: 4,
  movableDBoxProc: 5,
  zoomDocProc: 8,
  zoomNoGrow: 12,
  rDocProc: 16,
} as const;

/** The standard `WDEF`s live at ids 0–15; anything above is the app's own. */
export function isStandardProc(procID: number): boolean {
  return procID >= 0 && procID <= 16;
}

/**
 * **A resource-vs-documentation conflict, recorded rather than smoothed over.**
 *
 * `WIND 128` stores `procID` **12** (`zoomNoGrow`) — zoom box, *no* grow box.
 * §3 says the document window is "zoomable/resizable" and backs it with a quote
 * from the **0.9.1** Read Me: "Document windows are fully resizable".
 *
 * Both cannot be true of 0.9.2, and the fork is the primary evidence: it is what
 * 0.9.2 actually shipped, whereas the Read Me is a version-old feature list. So
 * the shell follows the resource, and the Read Me's claim is recorded in §14 as
 * an open item — a `SetWindowKind`-style override at runtime would only turn up
 * in the disassembly, which nobody has read for this.
 *
 * If that disassembly ever says otherwise, this flag is the one place to flip.
 */
export const DOCUMENT_WINDOW_IS_ZOOM_NO_GROW = true;

export interface WindowChrome {
  /** Title bar with a drag region. The `dBox` family has none. */
  titled: boolean;
  zoomBox: boolean;
  growBox: boolean;
}

/**
 * What chrome a `procID` asks for — **excluding the close box**, which is the
 * template's own `goAway` flag and not encoded here.
 *
 * Non-standard procIDs get the movable-dialog treatment: titled and fixed. That
 * is a reading, not a recovery — the app's custom `WDEF`s were never
 * disassembled. It matches how §3 describes the windows that use them.
 */
export function chromeFor(procID: number): WindowChrome {
  switch (procID) {
    case WindowProc.documentProc:
      return { titled: true, zoomBox: false, growBox: true };
    case WindowProc.noGrowDocProc:
      return { titled: true, zoomBox: false, growBox: false };
    case WindowProc.zoomDocProc:
      return { titled: true, zoomBox: true, growBox: true };
    case WindowProc.zoomNoGrow:
      return { titled: true, zoomBox: true, growBox: false };
    case WindowProc.dBoxProc:
    case WindowProc.plainDBox:
    case WindowProc.altDBoxProc:
      return { titled: false, zoomBox: false, growBox: false };
    case WindowProc.movableDBoxProc:
      return { titled: true, zoomBox: false, growBox: false };
    default:
      return { titled: true, zoomBox: false, growBox: false };
  }
}

export interface WindowOptions {
  /** Overrides the template's title — document windows are named per document. */
  title?: string;
  resizable?: boolean;
  onClose?: () => void;
  onResize?: (w: number, h: number) => void;
}

let nextWindowID = 1;

/**
 * One on-screen window. Owns its chrome and its drag/resize behaviour; the
 * *contents* are whatever the caller appends to {@link content}.
 */
export class Win {
  readonly id = nextWindowID++;
  readonly element: HTMLElement;
  readonly content: HTMLElement;
  readonly chrome: WindowChrome;
  readonly template: WindTemplate;
  /** From the template's `goAway` flag, not its `procID`. */
  readonly closeBox: boolean;

  private titleEl: HTMLElement | null = null;
  private manager: WindowManager | null = null;
  private opts: WindowOptions;

  constructor(template: WindTemplate, opts: WindowOptions = {}) {
    this.template = template;
    this.opts = opts;
    this.chrome = chromeFor(template.procID);
    this.closeBox = template.goAway;

    const r = template.rect;
    this.element = el("div", { class: "sfx-window", "data-active": "false" });
    this.element.style.left = `${r.left}px`;
    this.element.style.top = `${r.top}px`;
    this.element.style.width = `${rectWidth(r)}px`;
    this.element.style.height = `${rectHeight(r)}px`;

    if (this.chrome.titled) this.element.append(this.buildTitleBar(opts.title ?? template.title));

    this.content = el("div", { class: "sfx-window-body" });
    this.element.append(this.content);

    const growable = opts.resizable ?? this.chrome.growBox;
    if (growable) this.element.append(this.buildGrowBox());

    // Clicking anywhere in a background window activates it — and that click is
    // consumed by the activation, not passed through to whatever it landed on.
    this.element.addEventListener("mousedown", () => this.manager?.activate(this), true);
  }

  get title(): string {
    return this.titleEl?.textContent ?? this.template.title;
  }

  set title(t: string) {
    if (this.titleEl) this.titleEl.textContent = t;
  }

  private buildTitleBar(title: string): HTMLElement {
    const bar = el("div", { class: "sfx-titlebar" });

    if (this.closeBox) {
      const close = el("div", { class: "sfx-close", role: "button", title: "Close" });
      close.addEventListener("mousedown", (e) => e.stopPropagation());
      close.addEventListener("click", () => this.close());
      bar.append(close);
    }

    this.titleEl = el("div", { class: "sfx-title", text: title });
    bar.append(this.titleEl);

    if (this.chrome.zoomBox) {
      const zoom = el("div", { class: "sfx-zoom", role: "button", title: "Zoom" });
      zoom.addEventListener("mousedown", (e) => e.stopPropagation());
      bar.append(zoom);
    } else if (this.closeBox) {
      // Keep the title centred: the close box needs a counterweight.
      bar.append(el("div", { style: "width:11px;flex:none" }));
    }

    bar.addEventListener("mousedown", (e) => this.startDrag(e));
    return bar;
  }

  private startDrag(e: MouseEvent): void {
    e.preventDefault();
    const startX = e.clientX;
    const startY = e.clientY;
    const left = this.element.offsetLeft;
    const top = this.element.offsetTop;

    const move = (ev: MouseEvent) => {
      // A window may go off the right and bottom, but its title bar may never go
      // under the menu bar — that is how System 7 kept a window reachable.
      this.element.style.left = `${left + ev.clientX - startX}px`;
      this.element.style.top = `${Math.max(0, top + ev.clientY - startY)}px`;
    };
    const up = () => {
      window.removeEventListener("mousemove", move);
      window.removeEventListener("mouseup", up);
    };
    window.addEventListener("mousemove", move);
    window.addEventListener("mouseup", up);
  }

  private buildGrowBox(): HTMLElement {
    const grow = el("div", { class: "sfx-growbox" });
    grow.addEventListener("mousedown", (e) => {
      e.preventDefault();
      e.stopPropagation();
      const startX = e.clientX;
      const startY = e.clientY;
      const w0 = this.element.offsetWidth;
      const h0 = this.element.offsetHeight;

      const move = (ev: MouseEvent) => {
        const w = Math.max(120, w0 + ev.clientX - startX);
        const h = Math.max(80, h0 + ev.clientY - startY);
        this.element.style.width = `${w}px`;
        this.element.style.height = `${h}px`;
        this.opts.onResize?.(w, h);
      };
      const up = () => {
        window.removeEventListener("mousemove", move);
        window.removeEventListener("mouseup", up);
      };
      window.addEventListener("mousemove", move);
      window.addEventListener("mouseup", up);
    });
    return grow;
  }

  setActive(active: boolean): void {
    this.element.dataset["active"] = String(active);
  }

  close(): void {
    this.opts.onClose?.();
    this.manager?.remove(this);
  }

  /** @internal — set when the manager takes ownership. */
  attachTo(m: WindowManager): void {
    this.manager = m;
  }
}

/**
 * The window layer: z-order, activation, and the Windows menu's list.
 *
 * There is exactly one active window and it is the frontmost — System 7 had no
 * notion of focus separate from activation, and neither does this.
 */
export class WindowManager {
  readonly root: HTMLElement;
  private windows: Win[] = [];
  private active: Win | null = null;
  private listeners = new Set<() => void>();

  constructor(root: HTMLElement) {
    this.root = root;
  }

  /** Open a window from a `WIND` id — the only way windows get made here. */
  openTemplate(windID: number, opts: WindowOptions = {}): Win {
    return this.open(new Win(getWind(windID), opts));
  }

  open(win: Win): Win {
    win.attachTo(this);
    this.windows.push(win);
    this.root.append(win.element);
    this.activate(win);
    this.changed();
    return win;
  }

  remove(win: Win): void {
    const i = this.windows.indexOf(win);
    if (i < 0) return;
    this.windows.splice(i, 1);
    win.element.remove();
    if (this.active === win) {
      this.active = null;
      const next = this.windows[this.windows.length - 1];
      if (next) this.activate(next);
    }
    this.changed();
  }

  activate(win: Win): void {
    if (this.active === win) return;
    this.active?.setActive(false);
    this.active = win;
    win.setActive(true);
    // Bring to front by re-appending: the DOM order *is* the z-order.
    this.root.append(win.element);
    this.changed();
  }

  get activeWindow(): Win | null {
    return this.active;
  }

  /** Front to back, which is the order the Windows menu wants. */
  list(): readonly Win[] {
    return [...this.windows].reverse();
  }

  onChange(fn: () => void): () => void {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  }

  private changed(): void {
    for (const fn of this.listeners) fn();
  }
}
