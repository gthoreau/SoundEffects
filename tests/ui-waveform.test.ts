/**
 * The wave editor's maths and the info strip.
 *
 * The load-bearing test here is the unequal-channel one. §5 makes channels
 * drifting out of alignment the app's core weirdness, and every layer so far has
 * protected it — step 6 found that neither AIFF nor WAV can even *express* it.
 * A renderer is the easiest place in the whole program to quietly undo that: pad
 * the short lane to the longest, and the scars of a one-channel Clear vanish
 * from the only place the user could have seen them.
 *
 * `drawChannel` is exercised against a recording fake rather than a real canvas.
 * We are not testing that the browser can fill a rect; we are testing which
 * columns it is asked to fill, and where the drawing stops.
 */

import { describe, expect, it } from "vitest";
import { SoundDocument } from "../src/document.js";
import { setSample } from "../src/samples.js";
import { RATE_22K, doubleToFixed } from "../src/units.js";
import {
  DrawMode,
  MAX_ZOOM_BYTES_PER_PIXEL,
  byteToX,
  drawChannel,
  fitZoom,
  xToByte,
  zoomBy,
  zoomTo,
  type WaveView,
} from "../src/ui/waveform.js";
import {
  defaultInfoStripPrefs,
  documentSizeBytes,
  formatChannels,
  formatRate,
  formatRateAndSize,
  infoStripFields,
  selectedChannelLengthBytes,
} from "../src/ui/info-strip.js";

/** Records every fillRect the renderer asks for. */
function fakeCtx() {
  const fills: { x: number; y: number; w: number; h: number }[] = [];
  const ctx = {
    fills,
    save() {},
    restore() {},
    beginPath() {},
    rect() {},
    clip() {},
    strokeRect() {},
    fillStyle: "",
    strokeStyle: "",
    fillRect(x: number, y: number, w: number, h: number) {
      fills.push({ x, y, w, h });
    },
  };
  return ctx as unknown as CanvasRenderingContext2D & { fills: typeof fills };
}

const view = (over: Partial<WaveView> = {}): WaveView => ({
  firstByte: 0,
  bytesPerPixel: 1,
  mode: DrawMode.dots,
  ...over,
});

describe("zoom maths", () => {
  it("fitZoom spreads the sound across the width", () => {
    expect(fitZoom(1000, 100)).toBe(10);
    expect(fitZoom(50, 100)).toBe(0.5);
  });

  it("fitZoom never returns 0, whatever it is handed", () => {
    // A zero would divide by zero on the very next line of every caller.
    expect(fitZoom(0, 100)).toBe(1);
    expect(fitZoom(1000, 0)).toBe(1);
    expect(fitZoom(-5, 100)).toBe(1);
  });

  it("byteToX and xToByte round-trip on frame boundaries", () => {
    const v = view({ firstByte: 100, bytesPerPixel: 4 });
    expect(byteToX(100, v)).toBe(0);
    expect(byteToX(140, v)).toBe(10);
    expect(xToByte(10, v, 1)).toBe(140);
  });

  it("xToByte frame-aligns down, so a 16-bit view cannot land mid-sample", () => {
    const v = view({ firstByte: 0, bytesPerPixel: 3 });
    // x=1 → byte 3, which is not a 16-bit frame boundary; it must floor to 2.
    expect(xToByte(1, v, 2)).toBe(2);
    expect(xToByte(3, v, 2)).toBe(8);
  });

  it("zoomBy holds the anchor byte under the same pixel", () => {
    const v = view({ firstByte: 0, bytesPerPixel: 8 });
    const anchor = 400;
    const x0 = byteToX(anchor, v);
    const z = zoomBy(v, 0.5, anchor);
    expect(z.bytesPerPixel).toBe(4);
    expect(byteToX(anchor, z)).toBeCloseTo(x0, 6);
  });

  it("zoomBy never scrolls before the start of the sound", () => {
    const v = view({ firstByte: 0, bytesPerPixel: 8 });
    expect(zoomBy(v, 2, 0).firstByte).toBe(0);
  });

  it("zoom has a floor, so bytesPerPixel cannot reach 0", () => {
    let v = view({ bytesPerPixel: 1 });
    for (let i = 0; i < 40; i++) v = zoomBy(v, 0.5, 0);
    expect(v.bytesPerPixel).toBeGreaterThan(0);
    expect(v.bytesPerPixel).toBe(MAX_ZOOM_BYTES_PER_PIXEL);
  });

  /**
   * option-Zoom-In is "max zoom" (§3) — an absolute destination. It once
   * multiplied the current zoom by 1/256, which lands somewhere that depends on
   * where you started: from a fitted 15.5 bytes/px it reached 0.06, nowhere near
   * the 1/256 floor. "Max" has to mean max from anywhere.
   */
  it("zoomTo reaches max zoom from any starting zoom", () => {
    for (const start of [1, 15.5, 1000, 1 / 256]) {
      const z = zoomTo(view({ bytesPerPixel: start }), MAX_ZOOM_BYTES_PER_PIXEL, 0);
      expect(z.bytesPerPixel, `from ${start}`).toBe(MAX_ZOOM_BYTES_PER_PIXEL);
    }
  });

  it("zoomTo clamps past the floor rather than going through it", () => {
    expect(zoomTo(view(), 0, 0).bytesPerPixel).toBe(MAX_ZOOM_BYTES_PER_PIXEL);
    expect(zoomTo(view(), -5, 0).bytesPerPixel).toBe(MAX_ZOOM_BYTES_PER_PIXEL);
  });

  it("zoomTo holds the anchor byte under its pixel, like zoomBy", () => {
    const v = view({ firstByte: 0, bytesPerPixel: 8 });
    const anchor = 400;
    const x0 = byteToX(anchor, v);
    expect(byteToX(anchor, zoomTo(v, 2, anchor))).toBeCloseTo(x0, 6);
  });
});

describe("drawChannel honours unequal channel lengths (§5)", () => {
  it("stops at the channel's own end and leaves the rest background", () => {
    const doc = new SoundDocument({ bps: 8, numChans: 1, sizeBytes: 20 });
    const ch = doc.channels[0]!;
    for (let i = 0; i < 20; i++) setSample(ch.data, i, 100, 8);

    const ctx = fakeCtx();
    // 100px of width for a 20-byte channel at 1 byte/px: 80 columns have no data.
    drawChannel(ctx, ch, 8, view(), 0, 0, 100, 40);

    // The axis is one full-width fill; every other fill is a 1px wave column.
    const waveFills = ctx.fills.filter((f) => f.w === 1);
    expect(waveFills).toHaveLength(20);
    expect(Math.max(...waveFills.map((f) => f.x))).toBe(19);
  });

  it("draws a short lane short and a long lane long, in the same document", () => {
    // Exactly the shape a one-channel Clear leaves behind.
    const doc = new SoundDocument({ bps: 8, numChans: 2, sizeBytes: 30 });
    doc.channels[1]!.data = doc.channels[1]!.data.slice(0, 10);

    const count = (chIndex: number) => {
      const ctx = fakeCtx();
      drawChannel(ctx, doc.channels[chIndex]!, 8, view(), 0, 0, 100, 20);
      return ctx.fills.filter((f) => f.w === 1).length;
    };

    expect(count(0)).toBe(30);
    expect(count(1)).toBe(10);
  });

  it("draws nothing but the axis for an empty channel", () => {
    const doc = new SoundDocument({ bps: 8, numChans: 1, sizeBytes: 0 });
    const ctx = fakeCtx();
    drawChannel(ctx, doc.channels[0]!, 8, view(), 0, 0, 100, 40);
    // Background + axis, and not one wave column.
    expect(ctx.fills.filter((f) => f.w === 1)).toHaveLength(0);
  });
});

describe("Dots and Lines are different renderings, not a style (§9.1)", () => {
  /**
   * Below one sample per pixel the two modes genuinely disagree: Dots plots the
   * column's first sample (decimating, aliasing — what the 1994 app did), while
   * Lines spans the column's min/max so no peak is missed. On a signal that
   * alternates every sample, that difference is total.
   */
  it("Lines spans a column's min/max where Dots decimates to one sample", () => {
    const doc = new SoundDocument({ bps: 8, numChans: 1, sizeBytes: 64 });
    const ch = doc.channels[0]!;
    for (let i = 0; i < 64; i++) setSample(ch.data, i, i % 2 === 0 ? 127 : -128, 8);

    const render = (mode: typeof DrawMode.dots | typeof DrawMode.lines) => {
      const ctx = fakeCtx();
      drawChannel(ctx, ch, 8, view({ bytesPerPixel: 8, mode }), 0, 0, 8, 64);
      return ctx.fills.filter((f) => f.w === 1);
    };

    const dots = render(DrawMode.dots);
    const linesOut = render(DrawMode.lines);

    // Dots: every column is a 1px dot of the (always +127) first sample.
    expect(dots.every((f) => f.h === 1)).toBe(true);
    // Lines: every column spans the full excursion, so it is tall.
    expect(linesOut.every((f) => f.h > 1)).toBe(true);
    expect(Math.max(...linesOut.map((f) => f.h))).toBeGreaterThan(50);
  });

  it("agree at 1 byte/pixel, where there is nothing to decimate", () => {
    const doc = new SoundDocument({ bps: 8, numChans: 1, sizeBytes: 16 });
    const ch = doc.channels[0]!;
    for (let i = 0; i < 16; i++) setSample(ch.data, i, 60, 8);

    const render = (mode: typeof DrawMode.dots | typeof DrawMode.lines) => {
      const ctx = fakeCtx();
      drawChannel(ctx, ch, 8, view({ bytesPerPixel: 1, mode }), 0, 0, 16, 40);
      return ctx.fills.filter((f) => f.w === 1).map((f) => [f.x, f.y]);
    };

    expect(render(DrawMode.dots)).toEqual(render(DrawMode.lines));
  });
});

describe("the info strip (§3, §9 page 2)", () => {
  it("formats rate and size as §3's example does", () => {
    const doc = new SoundDocument({ rate: doubleToFixed(22050), bps: 16 });
    expect(formatRateAndSize(doc)).toBe("22.050 kHz; 16 bits");
  });

  it("says 'bit' singular — the model allows 1 bps", () => {
    const doc = new SoundDocument({ rate: doubleToFixed(22050), bps: 1 });
    expect(formatRateAndSize(doc)).toBe("22.050 kHz; 1 bit");
  });

  /**
   * The Mac's native rate is 22254.5454 Hz, which prints as 22.255 kHz at three
   * decimals. That is precisely why the Sampling Rate popup (MENU 155) spells it
   * "22.254 kHz – Standard" — it names the rate rather than rounding it.
   */
  it("prints the native Fixed rate at three decimals", () => {
    expect(formatRate(RATE_22K)).toBe("22.255 kHz");
    expect(formatRate(doubleToFixed(44100))).toBe("44.100 kHz");
  });

  it("uses the app's own words for channel counts", () => {
    expect(formatChannels(1)).toBe("Mono");
    expect(formatChannels(2)).toBe("Stereo");
    expect(formatChannels(5)).toBe("5 channels");
  });

  it("sums the file size over channels rather than assuming they match", () => {
    const doc = new SoundDocument({ bps: 8, numChans: 3, sizeBytes: 100 });
    doc.channels[1]!.data = doc.channels[1]!.data.slice(0, 40);
    // maxChSize × numChans would say 300. The buffers hold 240.
    expect(documentSizeBytes(doc)).toBe(240);
    expect(doc.maxChSize() * doc.numChans).toBe(300);
  });

  it("reports the SELECTED channel's length, not the document's", () => {
    const doc = new SoundDocument({ bps: 8, numChans: 2, sizeBytes: 100 });
    doc.channels[1]!.data = doc.channels[1]!.data.slice(0, 25);

    doc.firstSelCh = 1;
    expect(selectedChannelLengthBytes(doc)).toBe(100);
    doc.firstSelCh = 2;
    expect(selectedChannelLengthBytes(doc)).toBe(25);
  });

  it("honours each page-2 toggle independently", () => {
    const doc = new SoundDocument({ rate: doubleToFixed(22050), bps: 8, numChans: 2 });
    expect(infoStripFields(doc, defaultInfoStripPrefs)).toHaveLength(5);

    expect(
      infoStripFields(doc, { ...defaultInfoStripPrefs, showFileSize: false, showChannels: false }),
    ).toEqual(["22.050 kHz; 8 bits", "0 samples", "0.000 s"]);

    expect(
      infoStripFields(doc, {
        ...defaultInfoStripPrefs,
        showRateAndSize: false,
        showChannels: false,
        showFileSize: false,
        showLengthSamples: false,
        showLengthSeconds: false,
      }),
    ).toEqual([]);
  });
});
