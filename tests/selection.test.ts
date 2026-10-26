import { describe, it, expect } from "vitest";
import { SoundDocument } from "../src/document.js";
import {
  channelRange,
  resolveSelection,
  expandEmptySelection,
  getBytesToProcess,
  setSelection,
  setLoop,
  peakScopeDefault,
  selectLoop,
  extendToBeginning,
  extendToEnd,
  extendAbove,
  extendBelow,
  selectExactly,
  findPeak,
  selectPeak,
  selectSilence,
} from "../src/selection.js";

/** 8-bit doc from signed sample values (offset-binary encoded for you). */
function docFrom(channels: number[][], bps = 8): SoundDocument {
  const d = new SoundDocument({ bps, numChans: channels.length });
  channels.forEach((samples, i) => {
    d.channels[i]!.data = Uint8Array.from(samples.map((s) => (s ^ 0x80) & 0xff));
  });
  d.firstSelCh = 1;
  d.lastSelCh = channels.length;
  return d;
}

describe("channelRange — Gain.c's clamp-then-snap", () => {
  it("clamps a selection to each channel's own length", () => {
    const d = docFrom([[0, 0, 0, 0], [0, 0]]);
    setSelection(d, 0, 4);
    expect(channelRange(d, 0)).toEqual({ start: 0, end: 4 });
    // The short channel processes only the part it has.
    expect(channelRange(d, 1)).toEqual({ start: 0, end: 2 });
  });

  it("clamps before snapping, so a short 16-bit channel lands on a frame", () => {
    const d = new SoundDocument({ bps: 16, numChans: 1 });
    d.channels[0]!.data = new Uint8Array(5); // 2.5 frames — a ragged buffer
    expect(channelRange(d, 0, 0, 100)).toEqual({ start: 0, end: 4 });
  });

  it("snaps both ends down to a frame boundary", () => {
    const d = new SoundDocument({ bps: 16, numChans: 1 });
    d.channels[0]!.data = new Uint8Array(20);
    expect(channelRange(d, 0, 3, 9)).toEqual({ start: 2, end: 8 });
  });
});

describe("empty selection means everything — for effects", () => {
  it("expands a caret to the selected channels' extent, not the document's", () => {
    const d = docFrom([[0, 0], [0, 0, 0, 0, 0, 0]]);
    d.firstSelCh = 1;
    d.lastSelCh = 1;
    d.selSt = d.selEnd = 0;
    // maxRelChSize (2), not maxChSize (6).
    expect(resolveSelection(d)).toEqual({ start: 0, end: 2 });
  });

  it("leaves a real selection alone", () => {
    const d = docFrom([[0, 0, 0, 0]]);
    setSelection(d, 1, 3);
    expect(resolveSelection(d)).toEqual({ start: 1, end: 3 });
  });

  it("expandEmptySelection mutates in place like effect() does", () => {
    const d = docFrom([[0, 0, 0, 0]]);
    expandEmptySelection(d);
    expect([d.selSt, d.selEnd]).toEqual([0, 4]);
  });
});

describe("getBytesToProcess", () => {
  it("sums each channel's own intersection, short channels included as-is", () => {
    const d = docFrom([[0, 0, 0, 0], [0, 0]]);
    setSelection(d, 0, 4);
    expect(getBytesToProcess(d)).toBe(6); // 4 + 2, not 8
  });
});

describe("setSelection / setLoop", () => {
  it("frame-snaps and orders the bounds", () => {
    const d = new SoundDocument({ bps: 16, numChans: 1, sizeBytes: 20 });
    setSelection(d, 9, 3);
    expect([d.selSt, d.selEnd]).toEqual([2, 8]);
  });

  it("clamps to the longest channel", () => {
    const d = docFrom([[0, 0, 0, 0]]);
    setSelection(d, 0, 999);
    expect(d.selEnd).toBe(4);
  });

  it("Set Loop follows the selection", () => {
    const d = docFrom([[0, 0, 0, 0]]);
    setSelection(d, 1, 3);
    setLoop(d);
    expect([d.loopSt, d.loopEnd]).toEqual([1, 3]);
  });
});

describe("Select Exactly…", () => {
  it("selects N before, after, or centered on the selection", () => {
    const d = docFrom([new Array(20).fill(0)]);
    setSelection(d, 8, 12);
    selectExactly(d, 4, "before");
    expect([d.selSt, d.selEnd]).toEqual([4, 8]);

    setSelection(d, 8, 12);
    selectExactly(d, 4, "after");
    expect([d.selSt, d.selEnd]).toEqual([12, 16]);

    setSelection(d, 8, 12);
    selectExactly(d, 4, "centered");
    expect([d.selSt, d.selEnd]).toEqual([8, 12]);
  });

  it("clamps at the start of the sound", () => {
    const d = docFrom([new Array(20).fill(0)]);
    setSelection(d, 2, 2);
    selectExactly(d, 10, "before");
    expect(d.selSt).toBe(0);
  });
});

// ---------------------------------------------------------------------------
// Select Peak — disassembly/Select-Silence-Peak.md
// ---------------------------------------------------------------------------

describe("findPeak", () => {
  it("finds the loudest frame", () => {
    const d = docFrom([[0, 16, 0, 32, 0]]);
    expect(findPeak(d, 0, 5, 1, 1)).toEqual({ peakPos: 3, metric: 32 });
  });

  it("is absolute — a trough is a peak", () => {
    const d = docFrom([[0, 16, -100, 32, 0]]);
    expect(findPeak(d, 0, 5, 1, 1).peakPos).toBe(2);
  });

  it("gives ties to the earliest position", () => {
    const d = docFrom([[0, 32, 0, 32, 0]]);
    expect(findPeak(d, 0, 5, 1, 1).peakPos).toBe(1);
  });

  // The metric is |sum across channels|, NOT a per-channel max — so channels
  // that cancel each other produce no peak at all, however loud each one is.
  it("sums across channels, letting them cancel", () => {
    const d = docFrom([
      [0, 50, 10],
      [0, -50, 10],
    ]);
    const { peakPos, metric } = findPeak(d, 0, 3, 1, 2);
    expect(peakPos).toBe(2); // not 1, where the two channels sum to zero
    expect(metric).toBe(20);
  });

  it("skips channels shorter than the position instead of padding them", () => {
    const d = docFrom([
      [0, 5, 5, 40],
      [0, 5, 5],
    ]);
    // At pos 3 only channel 1 exists; it must still be found.
    expect(findPeak(d, 0, 4, 1, 2).peakPos).toBe(3);
  });

  it("reads 16-bit frames big-endian and steps a frame at a time", () => {
    const d = new SoundDocument({ bps: 16, numChans: 1 });
    d.channels[0]!.data = Uint8Array.from([0x00, 0x01, 0x7f, 0xff, 0x00, 0x02]);
    d.firstSelCh = 1;
    d.lastSelCh = 1;
    expect(findPeak(d, 0, 6, 1, 1)).toEqual({ peakPos: 2, metric: 32767 });
  });
});

describe("selectPeak", () => {
  it("selects exactly one frame with no margin", () => {
    const d = docFrom([[0, 16, 0, 32, 0]]);
    selectPeak(d, { scope: "selection", marginBytes: 0 });
    expect([d.selSt, d.selEnd]).toEqual([3, 4]);
  });

  it("widens symmetrically by the margin", () => {
    const d = docFrom([[0, 0, 0, 32, 0, 0, 0, 0]]);
    selectPeak(d, { scope: "selection", marginBytes: 2 });
    expect([d.selSt, d.selEnd]).toEqual([1, 6]);
  });

  it("clamps each end independently, without re-centering", () => {
    const d = docFrom([[32, 0, 0, 0]]);
    selectPeak(d, { scope: "selection", marginBytes: 2 });
    // Left clamps at 0; the right end is NOT extended to compensate.
    expect([d.selSt, d.selEnd]).toEqual([0, 3]);
  });

  it("clamps the right end at the selected channels' extent", () => {
    const d = docFrom([[0, 0, 0, 32]]);
    selectPeak(d, { scope: "selection", marginBytes: 2 });
    expect([d.selSt, d.selEnd]).toEqual([1, 4]);
  });

  it("'the Sound' scope searches every channel over the full document", () => {
    const d = docFrom([
      [0, 0],
      [0, 0, 0, 90],
    ]);
    d.firstSelCh = 1;
    d.lastSelCh = 1; // only channel 1 selected...
    selectPeak(d, { scope: "sound", marginBytes: 0 });
    expect(d.lastSelCh).toBe(2); // ...but "the Sound" forces all channels
    expect([d.selSt, d.selEnd]).toEqual([3, 4]);
  });

  it("honours function-key single-channel focus", () => {
    const d = docFrom([
      [0, 0, 0, 90],
      [0, 40, 0, 0],
    ]);
    selectPeak(d, { scope: "selection", marginBytes: 0, singleChannelFocus: 2 });
    expect([d.firstSelCh, d.lastSelCh]).toEqual([2, 2]);
    expect([d.selSt, d.selEnd]).toEqual([1, 2]);
  });
});

// ---------------------------------------------------------------------------
// Select Silence — disassembly/Select-Silence-Peak.md
// ---------------------------------------------------------------------------

describe("selectSilence", () => {
  it("does nothing when a real selection is up — it needs an insertion point", () => {
    const d = docFrom([[9, 0, 0, 0, 0, 9]]);
    setSelection(d, 1, 3);
    expect(selectSilence(d)).toBe(false);
    expect([d.selSt, d.selEnd]).toEqual([1, 3]);
  });

  it("selects the interior of the silent run, leaving a frame of margin", () => {
    // silence spans [2,6); the margins pull it in to [3,5).
    const d = docFrom([[9, 9, 0, 0, 0, 0, 9, 9]]);
    d.selSt = d.selEnd = 4;
    expect(selectSilence(d)).toBe(true);
    expect([d.selSt, d.selEnd]).toEqual([3, 5]);
  });

  it("has no threshold at all — one sample of ±1 ends the silence", () => {
    const d = docFrom([[9, 9, 0, 0, 1, 0, 0, 9]]);
    d.selSt = d.selEnd = 3;
    selectSilence(d);
    // The run is [2,4), not [2,7): the lone +1 at index 4 stops it dead.
    expect(d.selEnd).toBeLessThanOrEqual(4);
  });

  it("drops the margin where silence runs to the start of the channel", () => {
    const d = docFrom([[0, 0, 0, 9, 9]]);
    d.selSt = d.selEnd = 1;
    selectSilence(d);
    expect(d.selSt).toBe(0);
  });

  it("drops the margin where silence runs to the end of the channel", () => {
    const d = docFrom([[9, 9, 9, 9, 0, 0, 0, 0]]);
    d.selSt = d.selEnd = 5;
    selectSilence(d);
    expect(d.selEnd).toBe(8);
  });

  it("is a silent no-op when the caret is not inside silence", () => {
    const d = docFrom([[9, 9, 9, 9]]);
    d.selSt = d.selEnd = 2;
    expect(selectSilence(d)).toBe(false);
    expect([d.selSt, d.selEnd]).toEqual([2, 2]);
  });

  it("intersects the silent runs across the selected channels", () => {
    // ch1 silent [2,6), ch2 silent [3,7) -> interiors [3,5) and [4,6) -> [4,5).
    const d = docFrom([
      [9, 9, 0, 0, 0, 0, 9, 9],
      [9, 9, 9, 0, 0, 0, 0, 9],
    ]);
    d.selSt = d.selEnd = 4;
    expect(selectSilence(d)).toBe(true);
    expect([d.selSt, d.selEnd]).toEqual([4, 5]);
  });

  // Frames: [9][9][0][0][0][0][0][9] — silent frames at bytes 4..12.
  const wide = () => {
    const d = new SoundDocument({ bps: 16, numChans: 1 });
    d.channels[0]!.data = Uint8Array.from([
      0, 9, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9,
    ]);
    d.firstSelCh = 1;
    d.lastSelCh = 1;
    d.selSt = d.selEnd = 8;
    return d;
  };

  it("uses all-zero frames, not 0x80, at 16-bit", () => {
    const d = wide();
    expect(selectSilence(d)).toBe(true);
    expect(d.selSt).toBeGreaterThanOrEqual(4);
    expect(d.selEnd).toBeLessThanOrEqual(12);
  });

  // A faithful quirk, not a bug on our side. Select-Silence-Peak.md: "the
  // original does not fully normalize this". The backward scan stops on the last
  // nonzero *byte* — the low byte of the last loud frame — and `p += p % frameSize`
  // then pushes p up to the first silent frame's start, so `p + 2*frameSize` eats
  // TWO silent frames on the left while the forward side only ever eats one.
  // 8-bit is symmetric (one byte each side); wide frames are not.
  it("eats two frames of left margin but one of right, at 16-bit", () => {
    const d = wide();
    selectSilence(d);
    expect([d.selSt, d.selEnd]).toEqual([8, 12]);
    // Left: silent frames 4 and 6 both dropped. Right: only frame 12 dropped.
    expect(d.selSt - 4).toBe(2 * d.frameSize);
    expect(12 - d.selEnd).toBe(0);
  });
});

describe("Set Loop / Select Loop / Extend — CODE 4", () => {
  it("Select Loop selects the loop region across every channel", () => {
    const d = docFrom([[0, 0, 0, 0, 0, 0], [0, 0, 0, 0]]);
    setSelection(d, 1, 3);
    setLoop(d);
    d.firstSelCh = d.lastSelCh = 2;
    setSelection(d, 0, 0);
    expect(selectLoop(d)).toBe(true);
    expect([d.selSt, d.selEnd, d.firstSelCh, d.lastSelCh]).toEqual([1, 3, 1, 2]);
  });

  it("Extend to End stops at the selected channels' longest end", () => {
    const d = docFrom([[0, 0, 0, 0, 0, 0], [0, 0, 0, 0]]);
    d.firstSelCh = d.lastSelCh = 2;
    setSelection(d, 1, 2);
    expect(extendToEnd(d)).toBe(true);
    expect([d.selSt, d.selEnd]).toEqual([1, 4]);
    expect(extendToBeginning(d)).toBe(true);
    expect([d.selSt, d.selEnd]).toEqual([0, 4]);
  });

  it("Extend Above / Below add one channel, up to the edges", () => {
    const d = docFrom([[0], [0], [0]]);
    d.firstSelCh = d.lastSelCh = 2;
    expect(extendAbove(d)).toBe(true);
    expect(extendAbove(d)).toBe(false);
    expect(extendBelow(d)).toBe(true);
    expect(extendBelow(d)).toBe(false);
    expect([d.firstSelCh, d.lastSelCh]).toEqual([1, 3]);
  });
});

describe("Select Exactly — CODE 4 @0x2514", () => {
  it("After runs to the SELECTED channels' end at most", () => {
    const d = docFrom([new Array(20).fill(0), new Array(10).fill(0)]);
    d.firstSelCh = d.lastSelCh = 2;
    setSelection(d, 4, 6);
    selectExactly(d, 8, "after");
    expect([d.selSt, d.selEnd]).toEqual([6, 10]);
  });

  it("Centered on clamps its start at 0 without shifting the end", () => {
    const d = docFrom([new Array(20).fill(0)]);
    setSelection(d, 2, 4);
    selectExactly(d, 10, "centered");
    expect([d.selSt, d.selEnd]).toEqual([0, 10]);
  });

  it("Before ends where the selection started", () => {
    const d = docFrom([new Array(20).fill(0)]);
    setSelection(d, 10, 14);
    selectExactly(d, 3, "before");
    expect([d.selSt, d.selEnd]).toEqual([7, 10]);
  });
});

describe("Select Peak's opening radio", () => {
  it("is the Sound for a caret on every channel, the Selection otherwise", () => {
    const d = docFrom([[0, 0, 0], [0, 0]]);
    expect(peakScopeDefault(d)).toBe("sound");
    setSelection(d, 1, 2);
    expect(peakScopeDefault(d)).toBe("selection");
    setSelection(d, 0, 0);
    d.lastSelCh = 1;
    expect(peakScopeDefault(d)).toBe("selection");
  });
});
