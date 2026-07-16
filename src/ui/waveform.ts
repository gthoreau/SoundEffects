/**
 * The wave editor's middle zone — SPEC §3, §12 ("Waveform canvas with Dots/Lines
 * modes").
 *
 * Two things here are behaviour, not decoration, and both come straight out of
 * the model rather than from any drawing convention:
 *
 * - **Channels have independent lengths** (§2.1, §5). A lane is drawn to *its
 *   own* length and stops. It is never padded to the longest channel, because a
 *   channel that ends early is the visible scar of a one-channel Clear and the
 *   whole point of §5. The lane's leftover width stays background.
 * - **Dots was the original mode** (§9.1) — hence {@link DrawMode}'s default.
 *   Dots is not "lines with the strokes left off": at zoom levels below one
 *   sample per pixel it plots the *decimated* sample, while Lines spans the
 *   column's min/max. They disagree, and they are meant to.
 *
 * Sample reading goes through `getSample` (§7.4, `[SOURCE]`) so the packed
 * big-endian buffer and the 8-bit `XOR 0x80` are honoured here exactly as the
 * effects honour them. Drawing must never see a float.
 */

import type { Channel, SoundDocument } from "../document.js";
import { getSample, sampleValueLimits } from "../samples.js";

/** §9.1: "Draw the sound wave using • Dots / • Lines". Dots was the original. */
export const DrawMode = { dots: "dots", lines: "lines" } as const;
export type DrawModeName = (typeof DrawMode)[keyof typeof DrawMode];

export interface WaveColors {
  background: string;
  wave: string;
  axis: string;
  selection: string;
  frame: string;
}

/** Black on white with a 50%-grey selection wash — the 1-bit screen's palette. */
export const defaultColors: WaveColors = {
  background: "#ffffff",
  wave: "#000000",
  axis: "#aaaaaa",
  selection: "rgba(0, 0, 0, 0.25)",
  frame: "#000000",
};

/**
 * The view onto a channel: which byte is at the left edge, and how many bytes
 * each pixel covers.
 *
 * `bytesPerPixel` is the zoom, and it is *bytes*, not samples — the whole model
 * measures in bytes (§2.1), and at 16 bits a frame is two of them. Keeping the
 * unit consistent with the model is what stops zoom from drifting against the
 * selection, which is also stored in bytes.
 */
export interface WaveView {
  firstByte: number;
  bytesPerPixel: number;
  mode: DrawModeName;
}

/**
 * The tightest zoom the editor allows: 256 pixels per byte.
 *
 * A floor rather than a limit-of-taste — `bytesPerPixel` is a divisor in
 * `byteToX`, so zero would be a division by zero, and every zoom path clamps
 * here.
 */
export const MAX_ZOOM_BYTES_PER_PIXEL = 1 / 256;

/** Fit `bytes` into `width` pixels. Never returns 0 — that would divide by zero. */
export function fitZoom(bytes: number, width: number): number {
  if (width <= 0 || bytes <= 0) return 1;
  return Math.max(MAX_ZOOM_BYTES_PER_PIXEL, bytes / width);
}

/** The pixel column a byte offset lands in. */
export function byteToX(byte: number, view: WaveView): number {
  return (byte - view.firstByte) / view.bytesPerPixel;
}

/** The byte offset at a pixel column, frame-aligned down. */
export function xToByte(x: number, view: WaveView, frameSize: number): number {
  const raw = view.firstByte + x * view.bytesPerPixel;
  return Math.floor(raw / frameSize) * frameSize;
}

/**
 * Zoom in/out by a factor, holding a byte offset fixed under the cursor.
 * ⌘= / ⌘- (§3) use this with the selection's start as the anchor.
 */
export function zoomBy(view: WaveView, factor: number, anchorByte: number): WaveView {
  return zoomTo(view, view.bytesPerPixel * factor, anchorByte);
}

/**
 * Zoom to an absolute `bytesPerPixel`, holding `anchorByte` under its pixel.
 *
 * Separate from {@link zoomBy} because "max zoom" (option-Zoom-In, §3) is an
 * *absolute* destination, not a big multiply: multiplying the current zoom by
 * some large factor lands somewhere that depends on where you started, so
 * "max zoom" would mean something different every time.
 */
export function zoomTo(view: WaveView, bytesPerPixel: number, anchorByte: number): WaveView {
  const bpp = Math.max(MAX_ZOOM_BYTES_PER_PIXEL, bytesPerPixel);
  const anchorX = byteToX(anchorByte, view);
  return { ...view, bytesPerPixel: bpp, firstByte: Math.max(0, anchorByte - anchorX * bpp) };
}

/**
 * Draw one channel lane.
 *
 * The lane's vertical centre is silence. For 8-bit that is the offset-binary
 * midpoint and for 16-bit it is 0 — but `getSample` has already normalised both
 * into a signed value, so the centre is always "sample 0" here and the only
 * thing that varies is the full-scale divisor.
 */
export function drawChannel(
  ctx: CanvasRenderingContext2D,
  ch: Channel,
  bps: number,
  view: WaveView,
  x: number,
  y: number,
  width: number,
  height: number,
  colors: WaveColors = defaultColors,
): void {
  const frameSize = Math.ceil(bps / 8);
  const { max } = sampleValueLimits(bps);
  const mid = y + height / 2;
  const halfH = height / 2;
  const scale = max === 0 ? 0 : halfH / (max + 1);

  ctx.save();
  ctx.beginPath();
  ctx.rect(x, y, width, height);
  ctx.clip();

  ctx.fillStyle = colors.background;
  ctx.fillRect(x, y, width, height);

  // The silence axis spans the lane, not the sound: an empty channel still shows
  // where its centre is.
  ctx.fillStyle = colors.axis;
  ctx.fillRect(x, Math.round(mid), width, 1);

  const sizeBytes = ch.data.length;
  ctx.fillStyle = colors.wave;
  ctx.strokeStyle = colors.wave;

  for (let px = 0; px < width; px++) {
    const startByte = view.firstByte + px * view.bytesPerPixel;
    // The lane simply stops at this channel's own end. No padding, ever.
    if (startByte >= sizeBytes) break;
    const endByte = Math.min(startByte + view.bytesPerPixel, sizeBytes);

    const first = Math.floor(startByte / frameSize) * frameSize;
    const last = Math.floor((endByte - 1) / frameSize) * frameSize;

    if (view.mode === DrawMode.dots || last <= first) {
      // Dots: plot the column's first sample. Below 1 sample/pixel this
      // decimates — which is exactly what the 1994 app did, aliasing included.
      if (first >= 0 && first + frameSize <= sizeBytes) {
        const s = getSample(ch.data, first, bps);
        ctx.fillRect(x + px, Math.round(mid - s * scale), 1, 1);
      }
    } else {
      // Lines: span the column's min/max so no peak is skipped.
      let lo = Infinity;
      let hi = -Infinity;
      for (let b = first; b <= last; b += frameSize) {
        if (b + frameSize > sizeBytes) break;
        const s = getSample(ch.data, b, bps);
        if (s < lo) lo = s;
        if (s > hi) hi = s;
      }
      if (lo !== Infinity) {
        const yTop = Math.round(mid - hi * scale);
        const yBot = Math.round(mid - lo * scale);
        ctx.fillRect(x + px, yTop, 1, Math.max(1, yBot - yTop));
      }
    }
  }

  ctx.restore();
}

/** The selection wash over a lane. Drawn on top, so the wave shows through. */
export function drawSelection(
  ctx: CanvasRenderingContext2D,
  selSt: number,
  selEnd: number,
  view: WaveView,
  x: number,
  y: number,
  width: number,
  height: number,
  colors: WaveColors = defaultColors,
): void {
  const x0 = x + byteToX(selSt, view);
  const x1 = x + byteToX(selEnd, view);

  ctx.save();
  ctx.beginPath();
  ctx.rect(x, y, width, height);
  ctx.clip();

  if (selEnd <= selSt) {
    // A caret is an insertion point (§15 step 2's note) and draws as a 1px bar.
    ctx.fillStyle = colors.wave;
    ctx.fillRect(Math.round(x0), y, 1, height);
  } else {
    ctx.fillStyle = colors.selection;
    ctx.fillRect(x0, y, Math.max(1, x1 - x0), height);
  }
  ctx.restore();
}

/**
 * §3's "selection-position indicator shows where the current selection sits in
 * the whole sound" — a miniature of the entire document with the selection
 * marked, independent of zoom.
 */
export function drawPositionIndicator(
  ctx: CanvasRenderingContext2D,
  doc: SoundDocument,
  x: number,
  y: number,
  width: number,
  height: number,
  colors: WaveColors = defaultColors,
): void {
  const total = doc.maxChSize();
  ctx.fillStyle = colors.background;
  ctx.fillRect(x, y, width, height);
  ctx.strokeStyle = colors.frame;
  ctx.strokeRect(x + 0.5, y + 0.5, width - 1, height - 1);
  if (total <= 0) return;

  const x0 = x + (doc.selSt / total) * width;
  const x1 = x + (doc.selEnd / total) * width;
  ctx.fillStyle = colors.wave;
  ctx.fillRect(x0, y + 1, Math.max(1, x1 - x0), height - 2);
}
