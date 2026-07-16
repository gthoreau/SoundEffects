import { describe, it, expect } from "vitest";
import { SoundDocument } from "../src/document.js";
import { setSelection } from "../src/selection.js";
import {
  clear,
  copy,
  cut,
  paste,
  pasteMismatch,
  crop,
  silenceOut,
  silenceSelection,
  insertSilence,
  shiftSound,
  leaveSilence,
} from "../src/edit.js";
import { doubleToFixed } from "../src/units.js";

/** 8-bit doc from signed sample values. */
function docFrom(channels: number[][]): SoundDocument {
  const d = new SoundDocument({ bps: 8, numChans: channels.length });
  channels.forEach((samples, i) => {
    d.channels[i]!.data = Uint8Array.from(samples.map((s) => (s ^ 0x80) & 0xff));
  });
  d.firstSelCh = 1;
  d.lastSelCh = channels.length;
  return d;
}

/** Read a channel back as signed sample values. */
function samplesOf(d: SoundDocument, ch: number): number[] {
  return Array.from(d.channels[ch]!.data).map((b) => ((b ^ 0x80) << 24) >> 24);
}

describe("clear", () => {
  it("removes the range and shortens the channel", () => {
    const d = docFrom([[1, 2, 3, 4, 5]]);
    setSelection(d, 1, 3);
    expect(clear(d)).toBe(true);
    expect(samplesOf(d, 0)).toEqual([1, 4, 5]);
  });

  // The core weirdness of §5: this is a feature, not a bug to fix.
  it("shortens ONLY the selected channels, letting tracks drift apart", () => {
    const d = docFrom([
      [1, 2, 3, 4],
      [1, 2, 3, 4],
    ]);
    d.firstSelCh = 1;
    d.lastSelCh = 1;
    setSelection(d, 1, 3);
    clear(d);
    expect(samplesOf(d, 0)).toEqual([1, 4]);
    expect(samplesOf(d, 1)).toEqual([1, 2, 3, 4]);
    expect(d.channels[0]!.data.length).not.toBe(d.channels[1]!.data.length);
  });

  it("clears each channel's own intersection when they are unequal", () => {
    const d = docFrom([
      [1, 2, 3, 4, 5, 6],
      [1, 2, 3],
    ]);
    setSelection(d, 2, 6);
    clear(d);
    expect(samplesOf(d, 0)).toEqual([1, 2]);
    expect(samplesOf(d, 1)).toEqual([1, 2]); // only had one byte in range
  });

  // A caret is an insertion point, not "everything" — unlike an effect (Gain.c).
  it("does nothing with a caret, rather than wiping the document", () => {
    const d = docFrom([[1, 2, 3, 4]]);
    d.selSt = d.selEnd = 2;
    expect(clear(d)).toBe(false);
    expect(samplesOf(d, 0)).toEqual([1, 2, 3, 4]);
  });

  it("collapses the selection to a caret at the cut point", () => {
    const d = docFrom([[1, 2, 3, 4, 5]]);
    setSelection(d, 1, 3);
    clear(d);
    expect([d.selSt, d.selEnd]).toEqual([1, 1]);
  });
});

describe("copy", () => {
  it("puts real sound on the clipboard, carrying rate and bps", () => {
    const d = docFrom([[1, 2, 3, 4, 5]]);
    d.bps = 8;
    setSelection(d, 1, 4);
    const clip = copy(d)!;
    expect(clip).not.toBeNull();
    expect(clip.bps).toBe(d.bps);
    expect(clip.rate).toBe(d.rate);
    expect(samplesOf(clip, 0)).toEqual([2, 3, 4]);
  });

  it("does not disturb the document", () => {
    const d = docFrom([[1, 2, 3, 4, 5]]);
    setSelection(d, 1, 4);
    copy(d);
    expect(samplesOf(d, 0)).toEqual([1, 2, 3, 4, 5]);
  });

  it("copies only the selected channels", () => {
    const d = docFrom([
      [1, 2],
      [3, 4],
      [5, 6],
    ]);
    d.firstSelCh = 2;
    d.lastSelCh = 3;
    setSelection(d, 0, 2);
    const clip = copy(d)!;
    expect(clip.numChans).toBe(2);
    expect(samplesOf(clip, 0)).toEqual([3, 4]);
  });

  it("yields an unequal-length clipboard from unequal channels", () => {
    const d = docFrom([
      [1, 2, 3, 4],
      [1, 2],
    ]);
    setSelection(d, 0, 4);
    const clip = copy(d)!;
    expect(clip.channels[0]!.data.length).toBe(4);
    expect(clip.channels[1]!.data.length).toBe(2);
  });

  it("returns null for a caret", () => {
    const d = docFrom([[1, 2, 3]]);
    d.selSt = d.selEnd = 1;
    expect(copy(d)).toBeNull();
  });
});

describe("cut", () => {
  it("copies and then clears", () => {
    const d = docFrom([[1, 2, 3, 4, 5]]);
    setSelection(d, 1, 3);
    const clip = cut(d)!;
    expect(samplesOf(clip, 0)).toEqual([2, 3]);
    expect(samplesOf(d, 0)).toEqual([1, 4, 5]);
  });
});

describe("paste", () => {
  it("inserts at the caret", () => {
    const d = docFrom([[1, 2, 5]]);
    const clip = docFrom([[3, 4]]);
    d.selSt = d.selEnd = 2;
    expect(paste(d, clip)).toBe(1);
    expect(samplesOf(d, 0)).toEqual([1, 2, 3, 4, 5]);
  });

  it("replaces a non-empty selection", () => {
    const d = docFrom([[1, 9, 9, 5]]);
    const clip = docFrom([[3]]);
    setSelection(d, 1, 3);
    paste(d, clip);
    expect(samplesOf(d, 0)).toEqual([1, 3, 5]);
  });

  it("selects what it pasted", () => {
    const d = docFrom([[1, 5]]);
    const clip = docFrom([[3, 4]]);
    d.selSt = d.selEnd = 1;
    paste(d, clip);
    expect([d.selSt, d.selEnd]).toEqual([1, 3]);
  });

  it("pastes channel-for-channel starting at firstSelCh", () => {
    const d = docFrom([
      [1, 1],
      [2, 2],
    ]);
    const clip = docFrom([[7, 7]]);
    d.firstSelCh = 2;
    d.lastSelCh = 2;
    d.selSt = d.selEnd = 1;
    paste(d, clip);
    expect(samplesOf(d, 0)).toEqual([1, 1]); // untouched, so it drifts
    expect(samplesOf(d, 1)).toEqual([2, 7, 7, 2]);
  });

  it("drops surplus clipboard channels rather than growing the document", () => {
    const d = docFrom([[1, 1]]);
    const clip = docFrom([
      [7, 7],
      [8, 8],
    ]);
    d.selSt = d.selEnd = 0;
    expect(paste(d, clip)).toBe(1);
    expect(d.numChans).toBe(1);
  });

  it("pads a channel shorter than the caret before inserting", () => {
    const d = docFrom([
      [1, 2, 3, 4],
      [1],
    ]);
    const clip = docFrom([
      [7],
      [7],
    ]);
    d.selSt = d.selEnd = 3;
    paste(d, clip);
    expect(samplesOf(d, 1)).toEqual([1, 0, 0, 7]);
  });

  describe("format mismatch", () => {
    it("reports a bps mismatch for the resample prompt", () => {
      const d = docFrom([[1]]);
      const clip = docFrom([[1]]);
      clip.bps = 16;
      expect(pasteMismatch(d, clip)).toEqual({ bps: { from: 16, to: 8 } });
    });

    it("reports a rate mismatch", () => {
      const d = docFrom([[1]]);
      const clip = docFrom([[1]]);
      clip.rate = doubleToFixed(44100);
      expect(pasteMismatch(d, clip)?.rate).toEqual({
        from: clip.rate,
        to: d.rate,
      });
    });

    it("is quiet when the formats agree", () => {
      const d = docFrom([[1]]);
      expect(pasteMismatch(d, docFrom([[1]]))).toBeNull();
    });
  });
});

describe("crop", () => {
  it("keeps the selection and discards the rest", () => {
    const d = docFrom([[1, 2, 3, 4, 5]]);
    setSelection(d, 1, 4);
    expect(crop(d)).toBe(true);
    expect(samplesOf(d, 0)).toEqual([2, 3, 4]);
    expect([d.selSt, d.selEnd]).toEqual([0, 3]);
  });

  it("does nothing with a caret", () => {
    const d = docFrom([[1, 2, 3]]);
    d.selSt = d.selEnd = 1;
    expect(crop(d)).toBe(false);
    expect(samplesOf(d, 0)).toEqual([1, 2, 3]);
  });
});

describe("silenceOut", () => {
  it("zeroes a range without shortening the channel", () => {
    const d = docFrom([[1, 2, 3, 4]]);
    silenceOut(d, 1, 1, 1, 3);
    expect(samplesOf(d, 0)).toEqual([1, 0, 0, 4]);
    expect(d.channels[0]!.data.length).toBe(4);
  });

  it("writes 0x80 at 8-bit and 0x00 at 16-bit", () => {
    const eight = docFrom([[1, 2]]);
    silenceOut(eight, 1, 1, 0, 2);
    expect(Array.from(eight.channels[0]!.data)).toEqual([0x80, 0x80]);

    const wide = new SoundDocument({ bps: 16, numChans: 1 });
    wide.channels[0]!.data = Uint8Array.from([9, 9, 9, 9]);
    silenceOut(wide, 1, 1, 0, 4);
    expect(Array.from(wide.channels[0]!.data)).toEqual([0, 0, 0, 0]);
  });

  it("clamps per channel when lengths are unequal", () => {
    const d = docFrom([
      [1, 2, 3, 4],
      [1, 2],
    ]);
    silenceOut(d, 1, 2, 0, 4);
    expect(samplesOf(d, 0)).toEqual([0, 0, 0, 0]);
    expect(samplesOf(d, 1)).toEqual([0, 0]);
  });

  it("silenceSelection follows the current selection and channels", () => {
    const d = docFrom([[1, 2, 3, 4]]);
    setSelection(d, 2, 4);
    silenceSelection(d);
    expect(samplesOf(d, 0)).toEqual([1, 2, 0, 0]);
  });
});

describe("insertSilence", () => {
  it("inserts silence at the caret in every selected channel", () => {
    const d = docFrom([[1, 2]]);
    d.selSt = d.selEnd = 1;
    insertSilence(d, 2);
    expect(samplesOf(d, 0)).toEqual([1, 0, 0, 2]);
    expect([d.selSt, d.selEnd]).toEqual([1, 3]);
  });

  it("frame-snaps the amount", () => {
    const d = new SoundDocument({ bps: 16, numChans: 1, sizeBytes: 2 });
    d.selSt = d.selEnd = 0;
    insertSilence(d, 3); // 3 bytes -> 2
    expect(d.channels[0]!.data.length).toBe(4);
  });
});

describe("shiftSound", () => {
  it("shifts left, filling the tail with silence and dropping the head", () => {
    const d = docFrom([[1, 2, 3, 4]]);
    shiftSound(d, { kind: "sound" }, 1, "left");
    expect(samplesOf(d, 0)).toEqual([2, 3, 4, 0]);
  });

  it("shifts right, filling the head with silence and dropping the tail", () => {
    const d = docFrom([[1, 2, 3, 4]]);
    shiftSound(d, { kind: "sound" }, 1, "right");
    expect(samplesOf(d, 0)).toEqual([0, 1, 2, 3]);
  });

  it("preserves channel length", () => {
    const d = docFrom([[1, 2, 3, 4]]);
    shiftSound(d, { kind: "sound" }, 2, "right");
    expect(d.channels[0]!.data.length).toBe(4);
  });

  it("silences the lot when the shift exceeds the span", () => {
    const d = docFrom([[1, 2, 3]]);
    shiftSound(d, { kind: "sound" }, 99, "left");
    expect(samplesOf(d, 0)).toEqual([0, 0, 0]);
  });

  it("shifts a single channel, which is how tracks get misaligned on purpose", () => {
    const d = docFrom([
      [1, 2, 3],
      [1, 2, 3],
    ]);
    shiftSound(d, { kind: "channel", channel: 2 }, 1, "right");
    expect(samplesOf(d, 0)).toEqual([1, 2, 3]);
    expect(samplesOf(d, 1)).toEqual([0, 1, 2]);
  });

  it("confines a selection-scoped shift to the selected range", () => {
    const d = docFrom([[1, 2, 3, 4, 5, 6]]);
    setSelection(d, 1, 4);
    shiftSound(d, { kind: "selection" }, 1, "left");
    expect(samplesOf(d, 0)).toEqual([1, 3, 4, 0, 5, 6]);
  });

  it("shifts a channel range", () => {
    const d = docFrom([
      [1, 2],
      [1, 2],
      [1, 2],
    ]);
    shiftSound(d, { kind: "channels", first: 2, last: 3 }, 1, "right");
    expect(samplesOf(d, 0)).toEqual([1, 2]);
    expect(samplesOf(d, 1)).toEqual([0, 1]);
    expect(samplesOf(d, 2)).toEqual([0, 1]);
  });
});

describe("leaveSilence", () => {
  it("trims a long leading silence down to exactly N", () => {
    const d = docFrom([[0, 0, 0, 0, 1, 2]]);
    leaveSilence(d, 1, "sound");
    expect(samplesOf(d, 0)).toEqual([0, 1, 2]);
  });

  it("pads up to N when the sound starts abruptly", () => {
    const d = docFrom([[1, 2]]);
    leaveSilence(d, 2, "sound");
    expect(samplesOf(d, 0)).toEqual([0, 0, 1, 2]);
  });

  it("leaves an already-correct run alone", () => {
    const d = docFrom([[0, 1, 2]]);
    leaveSilence(d, 1, "sound");
    expect(samplesOf(d, 0)).toEqual([0, 1, 2]);
  });

  it("touches only the selected channels when scoped that way", () => {
    const d = docFrom([
      [0, 0, 0, 1],
      [0, 0, 0, 1],
    ]);
    d.firstSelCh = 2;
    d.lastSelCh = 2;
    leaveSilence(d, 1, "selectedChannels");
    expect(samplesOf(d, 0)).toEqual([0, 0, 0, 1]);
    expect(samplesOf(d, 1)).toEqual([0, 1]);
  });
});
