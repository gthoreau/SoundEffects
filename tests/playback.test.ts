/**
 * Import → playback, end to end.
 *
 * These tests exist because of a bug that shipped through step 6 with 59 format
 * tests and 91 audio tests passing over it. Each half was correct in isolation:
 * the decoder de-interleaved a stereo file perfectly, and `mixFrame` honoured
 * `outLeft`/`outRight` perfectly. But nothing set them — `documentFromChannels`
 * left every imported channel on `SoundDocument`'s new-channel default of
 * both-speakers-on, so a stereo file summed L and R into each ear and **played
 * as mono**.
 *
 * It was invisible because it lived in the *seam*: the format tests never mixed,
 * the audio tests never imported, and until step 7 wired a real backend there was
 * nothing to hear. So the test is the round trip — bytes to speakers — and the
 * assertion is that the two ears differ.
 */

import { describe, expect, it } from "vitest";
import { SoundDocument } from "../src/document.js";
import { setSample } from "../src/samples.js";
import { doubleToFixed } from "../src/units.js";
import { readSound, writeSound } from "../src/formats/index.js";
import { panStereoPair } from "../src/formats/pcm.js";
import { Transport } from "../src/audio/transport.js";
import { MODERN_CAPABILITIES } from "../src/audio/backend.js";
import { mixFrame, newMonitor } from "../src/audio/mixer.js";

/** A stereo document whose channels hold audibly different signals. */
function stereoDoc(rate = 22050, bps = 16, frames = 2000): SoundDocument {
  const fs = Math.ceil(bps / 8);
  const doc = new SoundDocument({
    name: "pair",
    rate: doubleToFixed(rate),
    bps,
    numChans: 2,
    sizeBytes: frames * fs,
  });
  for (let i = 0; i < frames; i++) {
    // Left: a steady tone. Right: silence-then-tone at a different amplitude.
    setSample(doc.channels[0]!.data, i * fs, Math.round(8000 * Math.sin(i / 8)), bps);
    setSample(doc.channels[1]!.data, i * fs, Math.round(3000 * Math.sin(i / 3)), bps);
  }
  return doc;
}

const rms = (a: Float32Array) => Math.sqrt(a.reduce((s, x) => s + x * x, 0) / a.length);

function render(doc: SoundDocument, frames = 4000): { left: Float32Array; right: Float32Array } {
  const t = new Transport(doc, { capabilities: MODERN_CAPABILITIES });
  t.play({});
  const left = new Float32Array(frames);
  const right = new Float32Array(frames);
  t.render([left, right], frames);
  return { left, right };
}

describe("a stereo file survives the round trip as stereo", () => {
  it.each([["wav"], ["aiff"]] as const)("through %s", (format) => {
    const bytes = writeSound(stereoDoc(), format);
    const doc = readSound(bytes, { name: "pair" });

    expect(doc.numChans).toBe(2);
    const { left, right } = render(doc);

    // The bug: both ears identical, because both channels fed both speakers.
    expect(rms(left)).toBeGreaterThan(0.01);
    expect(rms(right)).toBeGreaterThan(0.01);
    expect(rms(left)).not.toBeCloseTo(rms(right), 3);
  });

  it("routes channel 1 hard left and channel 2 hard right", () => {
    const doc = readSound(writeSound(stereoDoc(), "aiff"), { name: "pair" });
    expect(doc.channels[0]!.outLeft).toBe(true);
    expect(doc.channels[0]!.outRight).toBe(false);
    expect(doc.channels[1]!.outLeft).toBe(false);
    expect(doc.channels[1]!.outRight).toBe(true);
  });

  it("puts each channel's own samples in its own ear, and nothing else", () => {
    const doc = readSound(writeSound(stereoDoc(), "aiff"), { name: "pair" });
    // Silence channel 2 entirely; the left ear must be untouched by that.
    const before = mixFrame(doc, 0, { volume: 7, monitor: newMonitor() });
    doc.channels[1]!.volume = 0;
    const after = mixFrame(doc, 0, { volume: 7, monitor: newMonitor() });

    expect(after[0]).toBe(before[0]); // left unchanged
    expect(after[1]).toBe(0); // right gone
  });
});

describe("panStereoPair only claims what it can support", () => {
  it("leaves a mono document on both speakers", () => {
    // A mono sound must come out of both speakers — that IS the right default.
    const doc = new SoundDocument({ numChans: 1, sizeBytes: 16 });
    panStereoPair(doc);
    expect(doc.channels[0]!.outLeft).toBe(true);
    expect(doc.channels[0]!.outRight).toBe(true);
  });

  it("leaves 3+ channels alone — they are 'N channels', not a surround layout", () => {
    const doc = new SoundDocument({ numChans: 4, sizeBytes: 16 });
    panStereoPair(doc);
    for (const ch of doc.channels) {
      expect(ch.outLeft).toBe(true);
      expect(ch.outRight).toBe(true);
    }
  });

  it("is idempotent", () => {
    const doc = stereoDoc();
    panStereoPair(doc);
    panStereoPair(doc);
    expect(doc.channels.map((c) => [c.outLeft, c.outRight])).toEqual([
      [true, false],
      [false, true],
    ]);
  });
});

describe("playback honours the document, not the device", () => {
  it("resamples the declared rate to the device rate", () => {
    // 22050 Hz document on a 48 kHz device: ~2.18 device frames per sample, so
    // 4000 device frames consume ~1837 source frames. The point is that the
    // DOCUMENT's rate decides pitch — §6's load-bearing rule.
    const doc = readSound(writeSound(stereoDoc(22050, 16, 4000), "aiff"), { name: "pair" });
    const t = new Transport(doc, { capabilities: { ...MODERN_CAPABILITIES, deviceRate: 48000 } });
    t.play({});
    t.render([new Float32Array(4000), new Float32Array(4000)], 4000);
    // 4000 frames × (22050/48000) × 2 bytes ≈ 3675 bytes.
    expect(t.position).toBeGreaterThan(3600);
    expect(t.position).toBeLessThan(3750);
  });

  it("a lying rate field changes the pitch and touches no bytes (§6)", () => {
    const doc = readSound(writeSound(stereoDoc(22050, 16, 4000), "aiff"), { name: "pair" });
    const bytesBefore = Uint8Array.from(doc.channels[0]!.data);

    const slow = new Transport(doc, { capabilities: MODERN_CAPABILITIES });
    slow.play({});
    slow.render([new Float32Array(2000), new Float32Array(2000)], 2000);
    const slowPos = slow.position;

    doc.rate = doubleToFixed(44100); // Change Sound Info: header only.
    const fast = new Transport(doc, { capabilities: MODERN_CAPABILITIES });
    fast.play({});
    fast.render([new Float32Array(2000), new Float32Array(2000)], 2000);

    expect(fast.position).toBeGreaterThan(slowPos * 1.9);
    expect(doc.channels[0]!.data).toEqual(bytesBefore);
  });

  it("the volume ladder attenuates linearly, and Off is silent (§6)", () => {
    const doc = readSound(writeSound(stereoDoc(), "aiff"), { name: "pair" });
    const peakAt = (volume: number) => {
      const t = new Transport(doc, { capabilities: MODERN_CAPABILITIES, volume });
      t.play({});
      const l = new Float32Array(2000);
      t.render([l, new Float32Array(2000)], 2000);
      return l.reduce((m, x) => Math.max(m, Math.abs(x)), 0);
    };
    const full = peakAt(7);
    expect(peakAt(0)).toBe(0);
    expect(peakAt(3) / full).toBeCloseTo(3 / 7, 2);
  });

  it("unequal channels simply end, and the rest plays on", () => {
    const doc = readSound(writeSound(stereoDoc(), "aiff"), { name: "pair" });
    // The scar a one-channel Clear leaves. Right ends early; left keeps going.
    doc.channels[1]!.data = doc.channels[1]!.data.slice(0, 200);
    const { left, right } = render(doc, 4000);
    expect(rms(left)).toBeGreaterThan(0.01);
    // The right ear falls silent partway and the left does not.
    expect(rms(right.subarray(3000))).toBe(0);
    expect(rms(left.subarray(3000))).toBeGreaterThan(0.01);
  });
});
