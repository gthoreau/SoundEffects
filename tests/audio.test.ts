import { describe, it, expect } from "vitest";
import { SoundDocument, VOLUME_UNITY } from "../src/document.js";
import { setSelection, setLoop } from "../src/selection.js";
import { doubleToFixed, fixedToDouble } from "../src/units.js";
import {
  OfflineBackend,
  MODERN_CAPABILITIES,
  EIGHT_BIT_CAPABILITIES,
  canPlay,
  NOT_ENOUGH_HARDWARE,
  mixFrame,
  mixRange,
  newMonitor,
  isAudible,
  volumeScalar,
  floatDivisor,
  VOLUME_OFF,
  VOLUME_FULL,
  Transport,
  TransportState,
  PlayFrom,
  Vox,
  VoxState,
  inputLevel,
  Recorder,
  RecordState,
  RecordUse,
  ActiveChannels,
  InsertPolicy,
  DEFAULT_RECORD,
  DEFAULT_VOX,
  RECORD_RATES,
  auditionNote,
} from "../src/audio/index.js";
import {
  Keyboard,
  changePitch,
  pitchScale,
  auditionRate,
  semitonesFromBase,
  needsOneChannelAlert,
  keyboardChannel,
  changePitchOverflows,
  MAX_FIXED_RATE_HZ,
  KEYBOARD_ONE_CHANNEL_ONLY,
} from "../src/effects/keyboard.js";
import { Host, runEffect, kModCancel, kModNoError } from "../src/runtime/index.js";

function doc8(channels: number[][], rate = 22254.5454): SoundDocument {
  const d = new SoundDocument({ bps: 8, numChans: channels.length, rate: doubleToFixed(rate) });
  channels.forEach((samples, i) => {
    d.channels[i]!.data = Uint8Array.from(samples.map((s) => (s ^ 0x80) & 0xff));
  });
  d.firstSelCh = 1;
  d.lastSelCh = channels.length;
  return d;
}

const caps = (over: Partial<typeof MODERN_CAPABILITIES> = {}) => ({
  ...MODERN_CAPABILITIES,
  ...over,
});

// ---------------------------------------------------------------------------
// Capabilities — the modernised hardware gate
// ---------------------------------------------------------------------------

describe("the 16-bit hardware gate", () => {
  it("lets any depth through on a modern backend", () => {
    expect(canPlay(doc8([[1]]), MODERN_CAPABILITIES)).toBe(true);
    const wide = new SoundDocument({ bps: 16, numChans: 1, sizeBytes: 2 });
    expect(canPlay(wide, MODERN_CAPABILITIES)).toBe(true);
  });

  // The alert is not dead code — a genuinely limited backend gets the original's
  // voice back, verbatim.
  it("refuses 16-bit on a backend that cannot manage it", () => {
    const wide = new SoundDocument({ bps: 16, numChans: 1, sizeBytes: 2 });
    expect(canPlay(wide, EIGHT_BIT_CAPABILITIES)).toBe(false);
    expect(NOT_ENOUGH_HARDWARE).toContain("downsample it before playing");
  });

  it("still plays 8-bit on such a backend", () => {
    expect(canPlay(doc8([[1]]), EIGHT_BIT_CAPABILITIES)).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// Mixer
// ---------------------------------------------------------------------------

describe("the volume ladder", () => {
  it("has 8 steps, Off through 7", () => {
    expect(volumeScalar(VOLUME_OFF)).toBe(0);
    expect(volumeScalar(VOLUME_FULL)).toBe(1);
    expect(volumeScalar(4)).toBeCloseTo(4 / 7);
  });

  it("clamps out-of-range steps", () => {
    expect(volumeScalar(-1)).toBe(0);
    expect(volumeScalar(99)).toBe(1);
  });
});

describe("float conversion at the output edge", () => {
  it("maps full-scale negative to exactly -1.0", () => {
    expect(floatDivisor(8)).toBe(128);
    const d = doc8([[-128]]);
    expect(mixFrame(d, 0, { volume: VOLUME_FULL, monitor: newMonitor() })[0]).toBeCloseTo(-1);
  });

  it("maps 16-bit by its own container", () => {
    expect(floatDivisor(16)).toBe(32768);
    expect(floatDivisor(12)).toBe(32768); // the byte container, per §2.2
  });
});

describe("the mixer", () => {
  const m = (d: SoundDocument, pos = 0, over = {}) =>
    mixFrame(d, pos, { volume: VOLUME_FULL, monitor: newMonitor(), ...over });

  it("routes by each lane's speaker toggles", () => {
    const d = doc8([[64]]);
    d.channels[0]!.outLeft = true;
    d.channels[0]!.outRight = false;
    const [l, r] = m(d);
    expect(l).toBeCloseTo(0.5);
    expect(r).toBe(0);
  });

  it("silences a channel with both speakers off, muted or not", () => {
    const d = doc8([[64]]);
    d.channels[0]!.outLeft = false;
    d.channels[0]!.outRight = false;
    expect(m(d)).toEqual([0, 0]);
  });

  // The single most characteristic thing about playing a SoundEffects document.
  it("lets a short channel simply end while the rest play on", () => {
    const d = doc8([
      [64, 64, 64, 64],
      [64, 64],
    ]);
    // At pos 0 both contribute; at pos 3 only the long one does.
    expect(m(d, 0)[0]).toBeCloseTo(1.0); // 0.5 + 0.5
    expect(m(d, 3)[0]).toBeCloseTo(0.5); // ch2 has run out
  });

  it("applies each channel's 8.8 volume", () => {
    const d = doc8([[64]]);
    d.channels[0]!.volume = VOLUME_UNITY / 2; // 0.5
    expect(m(d)[0]).toBeCloseTo(0.25);
  });

  it("overdrives above unity on purpose, and clips at the edge", () => {
    const d = doc8([[64]]);
    d.channels[0]!.volume = VOLUME_UNITY * 4; // 4.0 -> 2.0, clipped
    expect(m(d)[0]).toBe(1);
  });

  it("treats volume <= 0 as silent", () => {
    const d = doc8([[64]]);
    d.channels[0]!.volume = 0;
    expect(m(d)).toEqual([0, 0]);
  });

  it("does not scale the sum by channel count — two loud channels clip", () => {
    const d = doc8([
      [100],
      [100],
    ]);
    // No headroom protection, exactly as Quick Mix and Sum refuse it.
    expect(m(d)[0]).toBe(1);
  });

  it("honours the master volume", () => {
    const d = doc8([[64]]);
    expect(m(d, 0, { volume: VOLUME_OFF })[0]).toBe(0);
    expect(m(d, 0, { volume: 4 })[0]).toBeCloseTo(0.5 * (4 / 7));
  });

  it("collapses to mono on a backend that mixes stereo down", () => {
    const d = doc8([[64]]);
    d.channels[0]!.outLeft = true;
    d.channels[0]!.outRight = false;
    const [l, r] = m(d, 0, { forceMono: true });
    expect(l).toBe(r);
  });

  it("renders a whole range", () => {
    const d = doc8([[0, 64, -64]]);
    const [left] = mixRange(d, 0, 3, { volume: VOLUME_FULL, monitor: newMonitor() });
    expect(Array.from(left).map((v) => +v.toFixed(2))).toEqual([0, 0.5, -0.5]);
  });
});

describe("function-key solo and mute", () => {
  it("mutes a channel with option-Fn", () => {
    const mon = newMonitor();
    mon.muted.add(1);
    expect(isAudible(mon, 0)).toBe(true);
    expect(isAudible(mon, 1)).toBe(false);
  });

  it("makes solo a filter — Fn plays that channel and nothing else", () => {
    const mon = newMonitor();
    mon.soloed.add(1);
    expect(isAudible(mon, 0)).toBe(false);
    expect(isAudible(mon, 1)).toBe(true);
  });

  it("lets mute win over solo", () => {
    const mon = newMonitor();
    mon.soloed.add(1);
    mon.muted.add(1);
    expect(isAudible(mon, 1)).toBe(false);
  });
});

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

describe("the transport", () => {
  const t = (d: SoundDocument, over = {}) =>
    new Transport(d, { capabilities: caps({ deviceRate: 1000 }), ...over });

  it("starts stopped", () => {
    expect(t(doc8([[1]])).currentState).toBe(TransportState.stopped);
  });

  it("plays and then stops at the end", () => {
    const d = doc8([[64, 64]], 1000); // device rate == doc rate: 1 byte per frame
    const tr = t(d);
    tr.play();
    const out = [new Float32Array(8), new Float32Array(8)];
    const n = tr.render(out, 8);
    expect(n).toBe(2);
    expect(tr.currentState).toBe(TransportState.stopped);
  });

  it("renders nothing while stopped or paused", () => {
    const tr = t(doc8([[64, 64]], 1000));
    const out = [new Float32Array(4), new Float32Array(4)];
    expect(tr.render(out, 4)).toBe(0);
    tr.play();
    tr.pause();
    expect(tr.render(out, 4)).toBe(0);
  });

  it("toggles with pause — space is play/pause", () => {
    const tr = t(doc8([[64, 64, 64, 64]], 1000));
    tr.play();
    tr.pause();
    expect(tr.currentState).toBe(TransportState.paused);
    tr.pause();
    expect(tr.currentState).toBe(TransportState.playing);
  });

  it("plays only the selection when there is one", () => {
    const d = doc8([[10, 20, 30, 40]], 1000);
    setSelection(d, 1, 3);
    const tr = t(d);
    tr.play();
    const out = [new Float32Array(8), new Float32Array(8)];
    expect(tr.render(out, 8)).toBe(2);
  });

  it("starts from the insertion point when the preference says so", () => {
    const d = doc8([[10, 20, 30, 40]], 1000);
    d.selSt = d.selEnd = 2; // a caret
    const tr = t(d, { playFrom: PlayFrom.insertionPoint });
    tr.play();
    expect(tr.position).toBe(2);
  });

  it("starts from the beginning when the preference says so", () => {
    const d = doc8([[10, 20, 30, 40]], 1000);
    d.selSt = d.selEnd = 2;
    const tr = t(d, { playFrom: PlayFrom.beginning });
    tr.play();
    expect(tr.position).toBe(0);
  });

  it("loops the selection with the Loop button", () => {
    const d = doc8([[10, 20, 30, 40]], 1000);
    setSelection(d, 0, 2);
    const tr = t(d);
    tr.play({ loop: true });
    const out = [new Float32Array(6), new Float32Array(6)];
    expect(tr.render(out, 6)).toBe(6); // never runs out
    expect(tr.currentState).toBe(TransportState.playing);
  });

  it("loops the Set Loop region with option-Play", () => {
    const d = doc8([[10, 20, 30, 40]], 1000);
    setSelection(d, 0, 1);
    setLoop(d, 2, 4); // a different region from the selection
    const tr = t(d);
    tr.play({ loop: true, useLoopRegion: true });
    expect(tr.position).toBe(2); // the loop region, not the selection
  });

  it("falls back to the whole sound when the loop region was never set", () => {
    const d = doc8([[10, 20, 30, 40]], 1000);
    const tr = t(d);
    tr.play({ loop: true, useLoopRegion: true });
    const out = [new Float32Array(8), new Float32Array(8)];
    expect(tr.render(out, 8)).toBe(8); // spins on the whole sound, not on nothing
  });

  it("seeks, frame-aligned", () => {
    const d = new SoundDocument({ bps: 16, numChans: 1, sizeBytes: 20 });
    const tr = t(d);
    tr.seek(7);
    expect(tr.position).toBe(6);
  });

  it("fast-forwards toward the end", () => {
    const d = doc8([new Array(100).fill(10)], 1000);
    const tr = t(d);
    tr.play();
    const before = tr.position;
    tr.fastForward();
    expect(tr.position).toBeGreaterThan(before);
  });

  it("solos a channel on request — F1..Fn", () => {
    const d = doc8([
      [100, 100],
      [50, 50],
    ], 1000);
    const tr = t(d);
    tr.play({ soloChannel: 1 });
    const out = [new Float32Array(2), new Float32Array(2)];
    tr.render(out, 2);
    // Only channel 2 sounds: 50/128, not 150/128.
    expect(out[0]![0]).toBeCloseTo(50 / 128);
  });
});

// The load-bearing rule of this whole step.
describe("the transport obeys the DECLARED rate, even when it lies", () => {
  it("advances at doc.rate / deviceRate", () => {
    // Document claims 2000 Hz, device runs at 1000: two document bytes per frame.
    const d = doc8([[10, 20, 30, 40]], 2000);
    const tr = new Transport(d, { capabilities: caps({ deviceRate: 1000 }) });
    tr.play();
    const out = [new Float32Array(8), new Float32Array(8)];
    expect(tr.render(out, 8)).toBe(2); // 4 bytes consumed at 2 per frame
  });

  it("changes pitch when the header is edited and no audio moves", () => {
    const d = doc8([new Array(8).fill(64)], 1000);
    const before = d.channels[0]!.data.slice();

    const slow = new Transport(d, { capabilities: caps({ deviceRate: 1000 }) });
    slow.play();
    const out = [new Float32Array(16), new Float32Array(16)];
    const slowFrames = slow.render(out, 16);

    // Change Sound Info / Change Pitch: edit the header only.
    d.rate = doubleToFixed(2000);
    expect(Array.from(d.channels[0]!.data)).toEqual(Array.from(before)); // not a byte

    const fast = new Transport(d, { capabilities: caps({ deviceRate: 1000 }) });
    fast.play();
    const fastFrames = fast.render(out, 16);

    // Same bytes, half the duration: the lie is audible, which is the point.
    expect(fastFrames).toBeLessThan(slowFrames);
    expect(fastFrames).toBe(4);
    expect(slowFrames).toBe(8);
  });
});

describe("the offline backend", () => {
  it("renders a document to floats with no device present", () => {
    const d = doc8([[0, 64, -64, 0]], 1000);
    const backend = new OfflineBackend({ deviceRate: 1000 });
    const tr = new Transport(d, { capabilities: backend.capabilities });
    tr.play();
    backend.start(tr);
    const [left] = backend.renderAll(2, 512, 100);
    expect(Array.from(left!).map((v) => +v.toFixed(2))).toEqual([0, 0.5, -0.5, 0]);
  });

  it("stops when the source runs out", () => {
    const d = doc8([[64]], 1000);
    const backend = new OfflineBackend({ deviceRate: 1000 });
    const tr = new Transport(d, { capabilities: backend.capabilities });
    tr.play();
    backend.start(tr);
    const [left] = backend.renderAll(2, 512, 100);
    expect(left!.length).toBe(1);
  });
});

// ---------------------------------------------------------------------------
// VOX
// ---------------------------------------------------------------------------

describe("input level", () => {
  it("reads peak magnitude on the sliders' 0-255 scale", () => {
    expect(inputLevel(Float32Array.from([0]))).toBe(0);
    expect(inputLevel(Float32Array.from([1]))).toBe(255);
    expect(inputLevel(Float32Array.from([-1]))).toBe(255);
    expect(inputLevel(Float32Array.from([0, 0.5, 0]), 0, 3)).toBe(128);
  });
});

describe("the VOX state machine", () => {
  const vox = (over: Partial<typeof DEFAULT_VOX> = {}) =>
    new Vox({ ...DEFAULT_VOX, ...over });

  it("records immediately when the start trigger is off", () => {
    const v = vox({ startTrigger: false });
    v.arm();
    expect(v.currentState).toBe(VoxState.recording);
    expect(v.process(0)).toBe(true); // keeps even silence
  });

  it("waits below the start threshold, keeping nothing", () => {
    const v = vox({ startTrigger: true, startThreshold: 100 });
    v.arm();
    expect(v.currentState).toBe(VoxState.monitoring);
    expect(v.process(50)).toBe(false);
    expect(v.currentState).toBe(VoxState.monitoring);
  });

  // No start delay: the attack survives.
  it("starts on the very first sample over threshold", () => {
    const v = vox({ startTrigger: true, startThreshold: 100 });
    v.arm();
    expect(v.process(120)).toBe(true);
    expect(v.currentState).toBe(VoxState.recording);
  });

  it("keeps recording forever when the stop trigger is off", () => {
    const v = vox({ startTrigger: false, stopTrigger: false });
    v.arm();
    for (let i = 0; i < 100; i++) expect(v.process(0)).toBe(true);
    expect(v.currentState).toBe(VoxState.recording);
  });

  it("ends the take at once when the stop delay is zero", () => {
    const v = vox({ startTrigger: false, stopTrigger: true, stopThreshold: 50, stopDelayFrames: 0 });
    v.arm();
    v.process(100);
    v.process(10);
    expect(v.currentState).toBe(VoxState.finished);
  });

  // The whole reason the stop delay exists.
  it("survives a pause shorter than the stop delay", () => {
    const v = vox({ startTrigger: false, stopTrigger: true, stopThreshold: 50, stopDelayFrames: 10 });
    v.arm();
    v.process(100);
    v.process(10, 5); // a 5-frame dip
    expect(v.currentState).toBe(VoxState.stopping);
    v.process(100); // back over the threshold
    expect(v.currentState).toBe(VoxState.recording);
  });

  it("ends the take once the stop delay elapses", () => {
    const v = vox({ startTrigger: false, stopTrigger: true, stopThreshold: 50, stopDelayFrames: 10 });
    v.arm();
    v.process(100);
    v.process(10, 5);
    expect(v.currentState).toBe(VoxState.stopping);
    v.process(10, 5);
    expect(v.currentState).toBe(VoxState.finished);
  });

  // The tail must survive too, not just the attack.
  it("keeps the frames spent waiting out the stop delay", () => {
    const v = vox({ startTrigger: false, stopTrigger: true, stopThreshold: 50, stopDelayFrames: 10 });
    v.arm();
    v.process(100);
    expect(v.process(10, 5)).toBe(true);
    expect(v.process(10, 5)).toBe(true);
  });

  // Independent triggers: stop > start is a patch, not a misconfiguration.
  it("allows a stop threshold above the start threshold — record until it gets LOUD", () => {
    const v = vox({
      startTrigger: true,
      startThreshold: 20,
      stopTrigger: true,
      stopThreshold: 200,
      stopDelayFrames: 0,
    });
    v.arm();
    // The block that trips the start trigger is kept whole — the stop condition
    // is not also evaluated on it, so every take is at least one block long.
    expect(v.process(50)).toBe(true);
    expect(v.currentState).toBe(VoxState.recording);
    // The next block, still under the (higher) stop threshold, ends it.
    v.process(50);
    expect(v.currentState).toBe(VoxState.finished);
  });

  it("stops on demand whatever the triggers think", () => {
    const v = vox({ startTrigger: false });
    v.arm();
    v.stop();
    expect(v.currentState).toBe(VoxState.finished);
    expect(v.process(255)).toBe(false);
  });
});

// ---------------------------------------------------------------------------
// Recorder
// ---------------------------------------------------------------------------

describe("the record rate popup", () => {
  it("offers the 1994 list, names and all", () => {
    expect(RECORD_RATES).toHaveLength(10);
    const labels = RECORD_RATES.map((r) => r.label);
    expect(labels).toContain("22.254 kHz (Standard)");
    expect(labels).toContain("24 kHz (Voice Recognition)");
    expect(labels).toContain("44.1 kHz (CD)");
    expect(labels).toContain("48 kHz (DAT)");
  });

  it("has the Mac's Standard rate match Robotize's calibration constant", () => {
    const std = RECORD_RATES.find((r) => r.label.includes("Standard"))!;
    expect(std.hz).toBeCloseTo(22254.5454, 3);
  });
});

describe("the recorder", () => {
  const rec = (over: Partial<typeof DEFAULT_RECORD> = {}) =>
    new Recorder({ ...DEFAULT_RECORD, ...over });
  const block = (v: number, n = 4) => [Float32Array.from(new Array(n).fill(v))];
  const stereoBlock = (l: number, r: number, n = 4) => [
    Float32Array.from(new Array(n).fill(l)),
    Float32Array.from(new Array(n).fill(r)),
  ];

  it("goes green then red — monitoring, then recording", () => {
    const r = rec({ vox: { ...DEFAULT_VOX, startTrigger: true, startThreshold: 100 } });
    r.start();
    expect(r.waveformColor).toBe("green");
    r.push(block(0.1)); // below threshold
    expect(r.waveformColor).toBe("green");
    r.push(block(0.9)); // over
    expect(r.waveformColor).toBe("red");
  });

  it("goes straight to red with no start trigger", () => {
    const r = rec();
    r.start();
    expect(r.waveformColor).toBe("red");
  });

  it("keeps nothing while monitoring", () => {
    const r = rec({ vox: { ...DEFAULT_VOX, startTrigger: true, startThreshold: 200 } });
    r.start();
    r.push(block(0.1));
    expect(r.frameCount).toBe(0);
  });

  it("captures frames while recording", () => {
    const r = rec();
    r.start();
    r.push(block(0.5, 8));
    expect(r.frameCount).toBe(8);
  });

  describe("the active-channels matrix", () => {
    it("mixes L and R for Mono+both", () => {
      const r = rec({ use: RecordUse.mono, activeChannels: ActiveChannels.both, bps: 8 });
      r.start();
      r.push(stereoBlock(0.25, 0.25));
      const doc = r.toDocument();
      expect(doc.numChans).toBe(1);
      // Summed, not averaged: 0.5 * 128 = 64.
      expect(((doc.channels[0]!.data[0]! ^ 0x80) << 24) >> 24).toBe(64);
    });

    it("takes the left input only for Mono+1", () => {
      const r = rec({ use: RecordUse.mono, activeChannels: ActiveChannels.left });
      r.start();
      r.push(stereoBlock(0.5, 0));
      const doc = r.toDocument();
      expect(((doc.channels[0]!.data[0]! ^ 0x80) << 24) >> 24).toBe(64);
    });

    it("takes the right input only for Mono+2", () => {
      const r = rec({ use: RecordUse.mono, activeChannels: ActiveChannels.right });
      r.start();
      r.push(stereoBlock(0, 0.5));
      const doc = r.toDocument();
      expect(((doc.channels[0]!.data[0]! ^ 0x80) << 24) >> 24).toBe(64);
    });

    it("makes two separate channels for Stereo", () => {
      const r = rec({ use: RecordUse.stereo });
      r.start();
      r.push(stereoBlock(0.5, 0.25));
      const doc = r.toDocument();
      expect(doc.numChans).toBe(2);
      expect(((doc.channels[0]!.data[0]! ^ 0x80) << 24) >> 24).toBe(64);
      expect(((doc.channels[1]!.data[0]! ^ 0x80) << 24) >> 24).toBe(32);
    });
  });

  it("beeps when clipping occurs, while recording", () => {
    const r = rec({ beepOnClipping: true });
    r.start();
    r.push(block(1.0, 3));
    expect(r.didClip).toBe(true);
    expect(r.beepCount).toBe(3);
  });

  it("stays quiet when the beep is off", () => {
    const r = rec({ beepOnClipping: false });
    r.start();
    r.push(block(1.0, 3));
    expect(r.beepCount).toBe(0);
  });

  it("honours the record cap", () => {
    const r = rec({ capKilobytes: 1, bps: 8 }); // 1024 bytes = 1024 frames
    r.start();
    for (let i = 0; i < 10; i++) r.push(block(0.5, 200));
    expect(r.frameCount).toBe(1024);
    expect(r.currentState).toBe(RecordState.done);
  });

  it("ignores the cap when it is VM-only and VM is off", () => {
    const r = rec({ capKilobytes: 1, capOnlyWithVM: true, virtualMemoryActive: false });
    r.start();
    for (let i = 0; i < 10; i++) r.push(block(0.5, 200));
    expect(r.frameCount).toBe(2000);
  });

  it("records the take at the requested rate and depth — the header tells the truth", () => {
    const r = rec({ rate: 44100, bps: 8 });
    r.start();
    r.push(block(0.5));
    const doc = r.toDocument();
    expect(fixedToDouble(doc.rate)).toBeCloseTo(44100);
    expect(doc.bps).toBe(8);
  });

  it("throws the take away on Cancel", () => {
    const r = rec();
    r.start();
    r.push(block(0.5, 8));
    r.cancel();
    expect(r.frameCount).toBe(0);
    expect(r.currentState).toBe(RecordState.idle);
  });

  describe("the insertion policy", () => {
    it("inserts the whole recorded sound at the caret", () => {
      const r = rec({ insertPolicy: InsertPolicy.whole, bps: 8 });
      r.start();
      r.push(block(0.5, 2)); // two frames of 64
      r.stop();
      const target = doc8([[1, 2, 3, 4]]);
      target.selSt = target.selEnd = 2;
      r.saveInto(target);
      expect(Array.from(target.channels[0]!.data).map((b) => ((b ^ 0x80) << 24) >> 24)).toEqual([
        1, 2, 64, 64, 3, 4,
      ]);
    });

    // Record into a hole: nothing after the selection moves.
    it("constrains the take to the selection, overwriting and shifting nothing", () => {
      const r = rec({ insertPolicy: InsertPolicy.constrainToSelection, bps: 8 });
      r.start();
      r.push(block(0.5, 8)); // eight frames, far more than the hole
      r.stop();
      const target = doc8([[1, 2, 3, 4, 5, 6]]);
      setSelection(target, 2, 4); // a two-byte hole
      r.saveInto(target);
      expect(Array.from(target.channels[0]!.data).map((b) => ((b ^ 0x80) << 24) >> 24)).toEqual([
        1, 2, 64, 64, 5, 6,
      ]);
      expect(target.channels[0]!.data.length).toBe(6); // length unchanged
    });
  });
});

// ---------------------------------------------------------------------------
// Keyboard — the sampler made of a rate multiplier
// ---------------------------------------------------------------------------

describe("Keyboard's pitch model", () => {
  it("makes an octave a doubling", () => {
    expect(pitchScale(12)).toBeCloseTo(2);
    expect(pitchScale(-12)).toBeCloseTo(0.5);
    expect(pitchScale(0)).toBe(1);
  });

  it("is twelve-tone equal temperament out of one pow()", () => {
    expect(pitchScale(7)).toBeCloseTo(1.4983, 4); // a fifth
  });

  it("measures semitones from the document's base note", () => {
    const d = doc8([[1]]);
    expect(d.baseFreq).toBe(60);
    expect(semitonesFromBase(d, 72)).toBe(12);
    expect(semitonesFromBase(d, 60)).toBe(0);
    expect(semitonesFromBase(d, 48)).toBe(-12);
  });

  it("auditions at the scaled rate without touching the document", () => {
    const d = doc8([[1]], 1000);
    const rate = auditionRate(d, 12);
    expect(fixedToDouble(rate)).toBeCloseTo(2000);
    expect(fixedToDouble(d.rate)).toBeCloseTo(1000); // unchanged
  });
});

describe("Change Pitch — the header lie", () => {
  it("edits the rate and not one byte of audio", () => {
    const d = doc8([[10, 20, 30, 40]], 1000);
    const before = d.channels[0]!.data.slice();
    changePitch(d, 12);
    expect(fixedToDouble(d.rate)).toBeCloseTo(2000);
    expect(Array.from(d.channels[0]!.data)).toEqual(Array.from(before));
  });

  it("re-reads every duration in the app without moving a sample", () => {
    // §2.3 derives seconds from samples/rate, so the sound's length in seconds
    // changes on a header edit alone.
    const d = doc8([new Array(1000).fill(0)], 1000);
    const lengthBefore = 1000 / fixedToDouble(d.rate); // 1 second
    changePitch(d, 12);
    const lengthAfter = 1000 / fixedToDouble(d.rate);
    expect(lengthBefore).toBeCloseTo(1);
    expect(lengthAfter).toBeCloseTo(0.5);
  });

  // Authentic quantisation: rate is Fixed 16.16, so every commit rounds.
  it("quantises each commit to 1/65536 Hz", () => {
    const d = doc8([[1]], 22254.5454);
    const exact = fixedToDouble(d.rate) * pitchScale(1);
    changePitch(d, 1);
    const stored = fixedToDouble(d.rate);
    expect(stored).not.toBe(exact); // a little is lost
    expect(stored).toBeCloseTo(exact, 4);
  });

  it("drifts at some rates when stepped semitone by semitone", () => {
    // At 8000 Hz, twelve +1 commits do NOT land on a single +12.
    const stepped = doc8([[1]], 8000);
    for (let i = 0; i < 12; i++) changePitch(stepped, 1);
    const once = doc8([[1]], 8000);
    changePitch(once, 12);
    expect(fixedToDouble(stepped.rate)).not.toBe(fixedToDouble(once.rate));
    expect(fixedToDouble(stepped.rate)).toBeCloseTo(16000, 3);
  });

  it("but agrees at others, because doubling a Fixed is itself exact", () => {
    // Don't assume drift either way — at 22254.5454 the two paths coincide.
    const stepped = doc8([[1]], 22254.5454);
    for (let i = 0; i < 12; i++) changePitch(stepped, 1);
    const once = doc8([[1]], 22254.5454);
    changePitch(once, 12);
    expect(fixedToDouble(stepped.rate)).toBe(fixedToDouble(once.rate));
  });
});

// The rate field once capped at ~65.5 kHz in an unsigned 32-bit container, so
// 88.2 kHz wrapped round to 22664 — "up an octave" played LOWER. That container
// was our choice, not a recovered behaviour (the original never met a rate it
// couldn't hold), so it was widened. See units.ts for the full argument.
describe("Change Pitch no longer overflows on real rates", () => {
  it("has a ceiling far beyond audio", () => {
    expect(MAX_FIXED_RATE_HZ).toBe(Math.pow(2, 37)); // ~137 GHz
  });

  // The widening must have kept the 16 fractional bits — the quantisation IS the
  // character of a Fixed rate, and it is what Robotize's delay constant and every
  // drift test depend on. If this ever passes exactly, the fraction was lost.
  it("still quantises to 1/65536 Hz — the part worth keeping", () => {
    const d = doc8([[1]], 22254.5454);
    expect(fixedToDouble(d.rate)).not.toBe(22254.5454);
    expect(fixedToDouble(d.rate)).toBeCloseTo(22254.5454, 4);
    // And the value is exactly what the old 32-bit arithmetic gave.
    expect(fixedToDouble(d.rate)).toBe(22254.54539489746);
  });

  it("leaves every value that fit in the old 32-bit container untouched", () => {
    // Robotize's NATIVE_RATE is the one that matters: it is a Fixed constant read
    // straight from the binary, and its meaning must not have shifted.
    expect(fixedToDouble(0x56ee8ba3)).toBe(22254.545455932617);
  });

  it("pitches a 44.1 kHz sample up an octave and gets 88.2 kHz", () => {
    const d = doc8([[1]], 44100);
    changePitch(d, 12);
    expect(fixedToDouble(d.rate)).toBe(88200);
    expect(changePitchOverflows(d, 12)).toBe(false);
  });

  it("handles the whole modern rate ladder", () => {
    for (const [from, expected] of [
      [48000, 96000],
      [96000, 192000],
      [88200, 176400],
    ] as const) {
      const d = doc8([[1]], from);
      changePitch(d, 12);
      expect(fixedToDouble(d.rate)).toBe(expected);
    }
  });

  it("still touches no audio while doing it", () => {
    const d = doc8([[10, 20, 30]], 44100);
    const before = d.channels[0]!.data.slice();
    changePitch(d, 12);
    expect(Array.from(d.channels[0]!.data)).toEqual(Array.from(before));
  });
});

describe("the Keyboard module", () => {
  it("plays only the first selected channel", () => {
    const d = doc8([
      [10, 20],
      [30, 40],
    ]);
    d.firstSelCh = 2;
    d.lastSelCh = 2;
    expect(keyboardChannel(d)).toBe(1);
  });

  it("apologises when it had to choose a channel", () => {
    const d = doc8([
      [10],
      [20],
    ]);
    d.firstSelCh = 1;
    d.lastSelCh = 2;
    expect(needsOneChannelAlert(d)).toBe(true);
    // STR# 1000 #3, verbatim from the shipping module — "Keboard" is the
    // original's own typo, and shipping it is the point.
    expect(KEYBOARD_ONE_CHANNEL_ONLY).toContain("I’m sorry");
    expect(KEYBOARD_ONE_CHANNEL_ONLY).toContain("Keboard");
  });

  it("does not apologise when there was no choice to make", () => {
    const d = doc8([[10]]);
    expect(needsOneChannelAlert(d)).toBe(false);
  });

  it("commits Change Pitch through effect()", () => {
    const d = doc8([[10, 20]], 1000);
    const before = d.channels[0]!.data.slice();
    const r = runEffect(Keyboard, d, new Host(), { semitones: 12 });
    expect(r).toBe(kModNoError); // "success, data modified" — the document did change
    expect(fixedToDouble(d.rate)).toBeCloseTo(2000);
    expect(Array.from(d.channels[0]!.data)).toEqual(Array.from(before)); // ...but not the audio
  });

  it("treats unison as a no-op", () => {
    const d = doc8([[10, 20]], 1000);
    expect(runEffect(Keyboard, d, new Host(), { semitones: 0 })).toBe(kModCancel);
  });
});

describe("auditioning a key", () => {
  it("plays the first selected channel at the key's pitch", () => {
    const d = doc8([
      [64, 64, 64, 64],
      [32, 32, 32, 32],
    ], 1000);
    d.firstSelCh = 1;
    d.lastSelCh = 1;
    const tr = new Transport(d, { capabilities: caps({ deviceRate: 1000 }) });
    const res = auditionNote(d, tr, 72)!; // an octave above base
    expect(res.channel).toBe(0);
    expect(res.rateScale).toBeCloseTo(2);
    expect(tr.currentState).toBe(TransportState.playing);
  });

  it("never touches the document — only Change Pitch commits", () => {
    const d = doc8([[64, 64]], 1000);
    const rateBefore = d.rate;
    const tr = new Transport(d, { capabilities: caps({ deviceRate: 1000 }) });
    auditionNote(d, tr, 72);
    expect(d.rate).toBe(rateBefore);
  });

  it("plays an octave up in half the frames — the multiplier IS the sampler", () => {
    const d = doc8([new Array(8).fill(64)], 1000);
    const out = [new Float32Array(16), new Float32Array(16)];

    const unison = new Transport(d, { capabilities: caps({ deviceRate: 1000 }) });
    auditionNote(d, unison, 60);
    const unisonFrames = unison.render(out, 16);

    const octave = new Transport(d, { capabilities: caps({ deviceRate: 1000 }) });
    auditionNote(d, octave, 72);
    const octaveFrames = octave.render(out, 16);

    expect(unisonFrames).toBe(8);
    expect(octaveFrames).toBe(4);
  });

  it("sustains by looping the selection when asked", () => {
    const d = doc8([[64, 64, 64, 64]], 1000);
    setSelection(d, 0, 2);
    const tr = new Transport(d, { capabilities: caps({ deviceRate: 1000 }) });
    auditionNote(d, tr, 60, true);
    const out = [new Float32Array(10), new Float32Array(10)];
    expect(tr.render(out, 10)).toBe(10); // held: it never runs out
  });

  it("reports the alert when more than one channel was selected", () => {
    const d = doc8([
      [64],
      [32],
    ]);
    const tr = new Transport(d, { capabilities: caps() });
    expect(auditionNote(d, tr, 60)!.alert).toBe(KEYBOARD_ONE_CHANNEL_ONLY);
  });
});
