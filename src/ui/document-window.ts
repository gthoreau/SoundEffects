/**
 * The document window — SPEC §3, from `WIND 128` (454×252 at (20, 50)).
 *
 * Three horizontal zones, in §3's order: the info strip, the wave editor, the
 * transport. This file owns their layout and the mouse behaviour over the lanes;
 * the zones themselves live in `info-strip.ts`, `waveform.ts` and
 * `transport-strip.ts`.
 *
 * The window never *edits* anything. Selection changes go through
 * `setSelection` (§2.4's clamp-then-snap order) and every command routes through
 * the menu bar into the core, so the mouse and the menu cannot disagree about
 * what a selection is.
 */

import type { SoundDocument } from "../document.js";
import { setSelection } from "../selection.js";
import { frameSizeFor } from "../samples.js";
import { el } from "./dom.js";
import { Res, getWind } from "./resources.js";
import { Win, WindowManager } from "./window.js";
import {
  DrawMode,
  type DrawModeName,
  MAX_ZOOM_BYTES_PER_PIXEL,
  type WaveView,
  defaultColors,
  drawChannel,
  drawPositionIndicator,
  drawSelection,
  fitZoom,
  xToByte,
  zoomBy,
  zoomTo,
} from "./waveform.js";
import {
  type InfoStripPrefs,
  defaultInfoStripPrefs,
  infoStripFields,
} from "./info-strip.js";
import { TransportStrip, type TransportButtonName } from "./transport-strip.js";

/** §3's zone heights. The lanes take whatever is left. */
const INFO_HEIGHT = 20;
const TRANSPORT_HEIGHT = 28;
const POSITION_INDICATOR_HEIGHT = 8;
/** Left gutter: the two speaker toggles per lane (§3 zone 2). */
const SPEAKER_GUTTER = 22;
/** Right gutter: the channel icon column plus the New Channel icon (§3 zone 2). */
const CHANNEL_COLUMN = 20;

export interface DocumentWindowOptions {
  drawMode?: DrawModeName;
  infoPrefs?: InfoStripPrefs;
  loop?: boolean;
  /** 0 = Off, 1–7 (§6's ladder). */
  volume?: number;
  onTransport?: (name: TransportButtonName) => void;
  onVolume?: (v: number) => void;
  onClose?: () => void;
  onSelectionChange?: () => void;
}

export class DocumentWindow {
  readonly win: Win;
  readonly doc: SoundDocument;
  view: WaveView;
  infoPrefs: InfoStripPrefs;

  private infoEl: HTMLElement;
  private canvas: HTMLCanvasElement;
  private transport: TransportStrip;
  private opts: DocumentWindowOptions;

  constructor(manager: WindowManager, doc: SoundDocument, opts: DocumentWindowOptions = {}) {
    this.doc = doc;
    this.opts = opts;
    this.infoPrefs = opts.infoPrefs ?? defaultInfoStripPrefs;
    this.view = {
      firstByte: 0,
      bytesPerPixel: 1,
      mode: opts.drawMode ?? DrawMode.dots,
    };

    // WIND 128 gives the frame; only the title is per-document.
    this.win = new Win(
      { ...getWind(Res.wind.document), title: doc.name },
      {
        title: doc.name,
        // WIND 128 is zoomNoGrow, but a browser window can be any size and a
        // fixed 454×252 canvas in 2026 would be a museum exhibit rather than a
        // usable editor. Resizing is allowed; see window.ts's
        // DOCUMENT_WINDOW_IS_ZOOM_NO_GROW for what the resource actually says.
        resizable: true,
        onClose: opts.onClose,
        onResize: () => this.layout(),
      },
    );

    this.infoEl = el("div", { class: "sfx-info-strip" });
    this.canvas = el("canvas", { class: "sfx-wave-canvas" });
    this.transport = new TransportStrip({
      onButton: (n) => opts.onTransport?.(n),
      onVolume: (v) => opts.onVolume?.(v),
      loop: opts.loop,
      volume: opts.volume,
    });

    this.win.content.append(this.infoEl, this.canvas, this.transport.element);
    this.canvas.addEventListener("mousedown", (e) => this.onCanvasMouseDown(e));

    manager.open(this.win);
    // layout() before zoomToFit(), and the order is load-bearing: zoomToFit
    // measures the canvas, and until layout has sized it the canvas reports the
    // HTML default 300×150 rather than the window's width. Fitting against that
    // opens every document zoomed out by ~40% with dead space to the right.
    this.layout();
    this.zoomToFit();
  }

  /** Lane geometry: one lane per channel, splitting the wave zone evenly. */
  private laneMetrics(): { top: number; height: number; x: number; width: number } {
    const rect = this.canvas.getBoundingClientRect();
    const usable = Math.max(1, rect.height - POSITION_INDICATOR_HEIGHT);
    const height = usable / Math.max(1, this.doc.numChans);
    return {
      top: 0,
      height,
      x: SPEAKER_GUTTER,
      width: Math.max(1, rect.width - SPEAKER_GUTTER - CHANNEL_COLUMN),
    };
  }

  private onCanvasMouseDown(e: MouseEvent): void {
    const rect = this.canvas.getBoundingClientRect();
    const m = this.laneMetrics();
    const frameSize = frameSizeFor(this.doc.bps);
    const localX = e.clientX - rect.left;

    // The canvas is wider than the lanes: §3 puts the two speaker toggles in a
    // left gutter and the channel icon column on the right. A press there is not
    // a click in the wave, and must not collapse the selection to byte 0 — which
    // is exactly what it would do, since the byte offset clamps at 0.
    if (localX < m.x || localX > m.x + m.width) return;

    const toByte = (clientX: number) =>
      Math.max(0, xToByte(clientX - rect.left - m.x, this.view, frameSize));

    const anchor = toByte(e.clientX);
    // Click = caret. Drag = range. Shift-click extends, matching shift-arrows (§4).
    if (e.shiftKey) {
      setSelection(this.doc, Math.min(this.doc.selSt, anchor), Math.max(this.doc.selEnd, anchor));
    } else {
      setSelection(this.doc, anchor, anchor);
    }
    this.selectionChanged();

    const move = (ev: MouseEvent) => {
      const b = toByte(ev.clientX);
      setSelection(this.doc, Math.min(anchor, b), Math.max(anchor, b));
      this.selectionChanged();
    };
    const up = () => {
      window.removeEventListener("mousemove", move);
      window.removeEventListener("mouseup", up);
    };
    window.addEventListener("mousemove", move);
    window.addEventListener("mouseup", up);
  }

  private selectionChanged(): void {
    this.draw();
    this.updateInfo();
    this.opts.onSelectionChange?.();
  }

  /** Fit in Window (§3's Zoom submenu, MENU 141). */
  zoomToFit(): void {
    const m = this.laneMetrics();
    this.view = { ...this.view, firstByte: 0, bytesPerPixel: fitZoom(this.doc.maxChSize(), m.width) };
    this.draw();
  }

  /** Fit Selection. An empty selection has nothing to fit, so it is a no-op. */
  zoomToSelection(): void {
    const m = this.laneMetrics();
    const len = this.doc.selEnd - this.doc.selSt;
    if (len <= 0) return;
    this.view = {
      ...this.view,
      firstByte: this.doc.selSt,
      bytesPerPixel: fitZoom(len, m.width),
    };
    this.draw();
  }

  /** ⌘= / ⌘-. The selection start is the anchor that stays put. */
  zoom(factor: number): void {
    this.view = zoomBy(this.view, factor, this.doc.selSt);
    this.draw();
  }

  /** option-Zoom-In (§3) — all the way in, not merely a long way in. */
  zoomToMax(): void {
    this.view = zoomTo(this.view, MAX_ZOOM_BYTES_PER_PIXEL, this.doc.selSt);
    this.draw();
  }

  setDrawMode(mode: DrawModeName): void {
    this.view = { ...this.view, mode };
    this.draw();
  }

  private layout(): void {
    const body = this.win.content;
    const h = body.clientHeight;
    const w = body.clientWidth;

    this.infoEl.style.height = `${INFO_HEIGHT}px`;
    this.transport.element.style.height = `${TRANSPORT_HEIGHT}px`;

    const waveH = Math.max(1, h - INFO_HEIGHT - TRANSPORT_HEIGHT);
    this.canvas.style.width = `${w}px`;
    this.canvas.style.height = `${waveH}px`;

    // Back the canvas at device resolution: a 1-px waveform must be 1 real pixel,
    // or the dots blur into the lines and the §9.1 preference stops meaning anything.
    const dpr = window.devicePixelRatio || 1;
    this.canvas.width = Math.round(w * dpr);
    this.canvas.height = Math.round(waveH * dpr);

    this.updateInfo();
    this.draw();
  }

  private updateInfo(): void {
    this.infoEl.replaceChildren(
      ...infoStripFields(this.doc, this.infoPrefs).map((t) =>
        el("span", { class: "sfx-info-field", text: t }),
      ),
    );
    const bg = this.infoPrefs.customBackground;
    this.canvas.style.background = bg ?? "";
  }

  draw(): void {
    const ctx = this.canvas.getContext("2d");
    if (!ctx) return;
    const dpr = window.devicePixelRatio || 1;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    const m = this.laneMetrics();
    const rect = this.canvas.getBoundingClientRect();
    const colors = this.infoPrefs.customBackground
      ? { ...defaultColors, background: this.infoPrefs.customBackground }
      : defaultColors;

    ctx.clearRect(0, 0, rect.width, rect.height);

    for (let i = 0; i < this.doc.numChans; i++) {
      const ch = this.doc.channels[i];
      if (!ch) continue;
      const top = m.top + i * m.height;
      drawChannel(ctx, ch, this.doc.bps, this.view, m.x, top, m.width, m.height, colors);

      // The selection wash covers only the selected channels — the 1-based
      // firstSelCh..lastSelCh range of §2.4, which is what an effect will act on.
      const chNum = i + 1;
      if (chNum >= this.doc.firstSelCh && chNum <= this.doc.lastSelCh) {
        drawSelection(ctx, this.doc.selSt, this.doc.selEnd, this.view, m.x, top, m.width, m.height, colors);
      }

      // The two speaker toggles, left of each lane (§3). They drive Quick Mix too.
      this.drawSpeakers(ctx, ch.outLeft, ch.outRight, 2, top + m.height / 2 - 8);
      ctx.strokeStyle = colors.frame;
      ctx.strokeRect(m.x - 0.5, top + 0.5, m.width + 1, m.height - 1);
    }

    drawPositionIndicator(
      ctx,
      this.doc,
      m.x,
      rect.height - POSITION_INDICATOR_HEIGHT,
      m.width,
      POSITION_INDICATOR_HEIGHT - 1,
      colors,
    );
  }

  /** Two 8×8 speaker toggles; filled = enabled. `[RES: ics# — see §14]` */
  private drawSpeakers(
    ctx: CanvasRenderingContext2D,
    outLeft: boolean,
    outRight: boolean,
    x: number,
    y: number,
  ): void {
    const box = (yy: number, on: boolean) => {
      ctx.strokeStyle = "#000";
      ctx.strokeRect(x + 0.5, yy + 0.5, 7, 7);
      ctx.fillStyle = on ? "#000" : "#fff";
      ctx.fillRect(x + 2, yy + 2, 4, 4);
    };
    box(y, outLeft);
    box(y + 9, outRight);
  }

  setLoop(on: boolean): void {
    this.transport.setLoop(on);
  }

  setVolume(v: number): void {
    this.transport.setVolume(v);
  }

  /**
   * Reflect the engine's state in the strip. Stop is only live while there is
   * something to stop — everything else stays available, because §6's transport
   * lets you hit Play while already playing (it restarts).
   */
  setTransportState(state: string): void {
    this.transport.setEnabled("stop", state !== "stopped");
    this.transport.setEnabled("pause", state !== "stopped");
  }

  refresh(): void {
    this.win.title = this.doc.name;
    this.updateInfo();
    this.draw();
  }
}
