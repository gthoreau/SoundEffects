/**
 * The document window's transport — SPEC §3 zone 3.
 *
 * "tape-recorder buttons: Record, Stop, Pause, Play, Fast Forward, Loop toggle,
 * Volume popup (Off, 1–7)" — the same six actions the Sound menu lists (§4), and
 * they share their handlers with it, because they are the same commands.
 *
 * **The glyphs here are drawn, not recovered.** Every other window's controls
 * come out of the fork — the Record window's four buttons are `BTNs 134`, the
 * Mixer's fader is `CNTL 200` — but the document window's transport is neither a
 * `CNTL` nor a `BTNs` list, and the icon suites in the fork are cursors, channel
 * icons and the trash can. So the app drew this strip itself, from a `PICT` or in
 * code, and which is unrecovered. Tape-transport symbols are universal enough
 * that drawing them is safe; claiming they are the original's pixels would not
 * be. Recorded as an open item in §14.
 */

import { el } from "./dom.js";

/** The six transport actions, in §3's left-to-right order. */
export const TransportButton = {
  record: "record",
  stop: "stop",
  pause: "pause",
  play: "play",
  fastForward: "fastForward",
  loop: "loop",
} as const;
export type TransportButtonName =
  (typeof TransportButton)[keyof typeof TransportButton];

export const TRANSPORT_ORDER: TransportButtonName[] = [
  TransportButton.record,
  TransportButton.stop,
  TransportButton.pause,
  TransportButton.play,
  TransportButton.fastForward,
  TransportButton.loop,
];

/** Tape-deck glyphs as SVG paths on a 16×16 grid. Drawn; see the file header. */
const GLYPHS: Record<TransportButtonName, string> = {
  record: "M8 3a5 5 0 1 0 0 10A5 5 0 0 0 8 3z",
  stop: "M4 4h8v8H4z",
  pause: "M4.5 4h2.5v8H4.5zM9 4h2.5v8H9z",
  play: "M5 3.5l7 4.5-7 4.5z",
  fastForward: "M3 3.5l5 4.5-5 4.5zM8.5 3.5l5 4.5-5 4.5z",
  loop: "M4 5h7v2l3-3-3-3v2H2v5h2zM12 11H5V9l-3 3 3 3v-2h9V8h-2z",
};

const TITLES: Record<TransportButtonName, string> = {
  record: "Record",
  stop: "Stop",
  pause: "Pause",
  play: "Play",
  fastForward: "Fast Forward",
  loop: "Loop",
};

function glyphButton(name: TransportButtonName): HTMLElement {
  const b = el("div", {
    class: "sfx-transport-button",
    role: "button",
    title: TITLES[name],
    "data-button": name,
    "aria-label": TITLES[name],
  });
  b.innerHTML =
    `<svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true">` +
    `<path d="${GLYPHS[name]}" fill="currentColor"/></svg>`;
  return b;
}

export interface TransportStripOptions {
  onButton: (name: TransportButtonName) => void;
  onVolume: (volume: number) => void;
  /** 0 = Off, 1–7. */
  volume?: number;
  loop?: boolean;
}

/**
 * The strip. Stateless about audio — it reports clicks and reflects what it is
 * told, so the Sound menu and these buttons cannot drift apart.
 */
export class TransportStrip {
  readonly element: HTMLElement;
  private buttons = new Map<TransportButtonName, HTMLElement>();
  private volumePopup: HTMLSelectElement;
  private loopOn: boolean;

  constructor(opts: TransportStripOptions) {
    this.loopOn = opts.loop ?? false;
    this.element = el("div", { class: "sfx-transport" });

    for (const name of TRANSPORT_ORDER) {
      const b = glyphButton(name);
      // Greying a button is not the same as disabling it: these are <div>s, not
      // <button>s, so `aria-disabled` styles them but the browser still delivers
      // the click. The guard has to be here.
      b.addEventListener("click", () => {
        if (b.getAttribute("aria-disabled") === "true") return;
        opts.onButton(name);
      });
      this.buttons.set(name, b);
      this.element.append(b);
    }

    // Volume: Off, 1–7 — the eight-step ladder of §6, straight from MENU 142.
    this.volumePopup = el("select", { class: "sfx-popup sfx-volume", title: "Volume" });
    for (let v = 0; v <= 7; v++) {
      this.volumePopup.append(el("option", { value: v, text: v === 0 ? "Off" : String(v) }));
    }
    this.volumePopup.value = String(opts.volume ?? 7);
    this.volumePopup.addEventListener("change", () =>
      opts.onVolume(Number(this.volumePopup.value)),
    );

    this.element.append(el("div", { class: "sfx-transport-spacer" }));
    this.element.append(this.volumePopup);
    this.setLoop(this.loopOn);
  }

  /** Loop is a toggle, and a toggled-on System 7 icon button is *inverted*. */
  setLoop(on: boolean): void {
    this.loopOn = on;
    this.buttons.get(TransportButton.loop)?.setAttribute("aria-pressed", String(on));
  }

  get loop(): boolean {
    return this.loopOn;
  }

  setVolume(v: number): void {
    this.volumePopup.value = String(v);
  }

  setEnabled(name: TransportButtonName, enabled: boolean): void {
    this.buttons.get(name)?.setAttribute("aria-disabled", String(!enabled));
  }
}
