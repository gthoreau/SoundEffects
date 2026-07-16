import { describe, it, expect } from "vitest";
import { SoundDocument, VOLUME_UNITY } from "../src/document.js";

describe("SoundDocument defaults", () => {
  it("opens as an 8-bit mono 22 kHz sound with a MIDI base of 60", () => {
    const doc = new SoundDocument();
    expect(doc.bps).toBe(8);
    expect(doc.numChans).toBe(1);
    expect(doc.baseFreq).toBe(60);
    expect(doc.frameSize).toBe(1);
    expect(doc.channels[0]!.volume).toBe(VOLUME_UNITY);
  });

  it("fills new channels with the depth's silent byte, not raw zeros", () => {
    const eight = new SoundDocument({ bps: 8, sizeBytes: 4 });
    expect(Array.from(eight.channels[0]!.data)).toEqual([0x80, 0x80, 0x80, 0x80]);
    const sixteen = new SoundDocument({ bps: 16, sizeBytes: 4 });
    expect(Array.from(sixteen.channels[0]!.data)).toEqual([0, 0, 0, 0]);
  });
});

describe("unequal channel lengths", () => {
  const doc = () => {
    const d = new SoundDocument({ numChans: 3, sizeBytes: 0 });
    d.channels[0]!.data = new Uint8Array(10);
    d.channels[1]!.data = new Uint8Array(40);
    d.channels[2]!.data = new Uint8Array(25);
    return d;
  };

  it("maxChSize spans all channels", () => {
    expect(doc().maxChSize()).toBe(40);
  });

  it("maxRelChSize spans only the selected ones", () => {
    const d = doc();
    d.firstSelCh = 1;
    d.lastSelCh = 1;
    expect(d.maxRelChSize()).toBe(10);
    d.lastSelCh = 3;
    expect(d.maxRelChSize()).toBe(40);
    d.firstSelCh = 3;
    expect(d.maxRelChSize()).toBe(25);
  });
});

describe("channel column operations", () => {
  it("appends a channel by default", () => {
    const d = new SoundDocument({ numChans: 1 });
    d.addChannel();
    expect(d.numChans).toBe(2);
  });

  it("inserts at a 1-based position", () => {
    const d = new SoundDocument({ numChans: 2 });
    d.channels[0]!.volume = 111;
    d.addChannel(1);
    expect(d.numChans).toBe(3);
    expect(d.channels[1]!.volume).toBe(111);
  });

  // "I'm sorry, but I can't delete the only channel of a mono sound.
  //  Delete the document instead."
  it("refuses to delete the only channel of a mono sound", () => {
    const d = new SoundDocument({ numChans: 1 });
    expect(d.delChannel(1)).toBe(false);
    expect(d.numChans).toBe(1);
  });

  it("deletes a channel and renumbers its chunks away", () => {
    const d = new SoundDocument({ numChans: 3 });
    d.chunks = [
      { name: "a", start: 0, length: 1, channel: 1, kind: "Chunk" },
      { name: "b", start: 0, length: 1, channel: 2, kind: "Chunk" },
      { name: "c", start: 0, length: 1, channel: 3, kind: "Chunk" },
    ];
    expect(d.delChannel(2)).toBe(true);
    expect(d.numChans).toBe(2);
    expect(d.chunks.map((c) => [c.name, c.channel])).toEqual([
      ["a", 1],
      ["c", 2],
    ]);
  });

  it("swaps two channels", () => {
    const d = new SoundDocument({ numChans: 2 });
    d.channels[0]!.volume = 1;
    d.channels[1]!.volume = 2;
    d.swapChannels(1, 2);
    expect(d.channels.map((c) => c.volume)).toEqual([2, 1]);
  });

  it("keeps the channel selection inside the document after a delete", () => {
    const d = new SoundDocument({ numChans: 3 });
    d.firstSelCh = 2;
    d.lastSelCh = 3;
    d.delChannel(3);
    expect(d.lastSelCh).toBeLessThanOrEqual(d.numChans);
  });
});

describe("clone", () => {
  it("deep-copies the buffers so the clipboard cannot alias the document", () => {
    const d = new SoundDocument({ numChans: 1, sizeBytes: 4, bps: 8 });
    const c = d.clone();
    c.channels[0]!.data[0] = 0x00;
    expect(d.channels[0]!.data[0]).toBe(0x80);
  });
});
