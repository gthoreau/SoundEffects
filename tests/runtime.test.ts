import { describe, it, expect } from "vitest";
import { SoundDocument } from "../src/document.js";
import { getSample, setSample } from "../src/samples.js";
import { doubleToFixed } from "../src/units.js";
import {
  AnsiCRandom,
  ToolboxRandom,
  Host,
  DoExtend,
  Msg,
  dispatch,
  kHeadersVersion,
  kModCancel,
  kModHasNoSettings,
  kModNoError,
  kModUnknownMessage,
  kTellGenericError,
  kModGenericError,
  dataWasChanged,
  errorVoice,
  isError,
  ItemType,
  validateSchema,
  captureSetup,
  applySetup,
  modResampleSoundBits,
  modResampleSoundRate,
  type EffectModule,
} from "../src/runtime/index.js";

describe("result codes — the sign carries the meaning", () => {
  it("says whether data was changed", () => {
    expect(dataWasChanged(kModNoError)).toBe(true);
    expect(dataWasChanged(kModCancel)).toBe(false);
    // Positive = error, data untouched.
    expect(dataWasChanged(kModGenericError)).toBe(false);
    // Negative = error, but data WAS changed — the host must warn.
    expect(dataWasChanged(kTellGenericError)).toBe(true);
  });

  it("does not treat cancel as an error", () => {
    expect(isError(kModCancel)).toBe(false);
    expect(isError(kModNoError)).toBe(false);
    expect(isError(kModGenericError)).toBe(true);
  });

  // §7.7: the absolute value indexes the module's own STR# 1000.
  it("finds the module's own voice by |result|", () => {
    const strings = ["first", "second", "third"];
    expect(errorVoice(3, strings)).toBe("third");
    expect(errorVoice(-3, strings)).toBe("third"); // absolute value
    expect(errorVoice(kModNoError, strings)).toBeNull();
    expect(errorVoice(99, strings)).toBeNull(); // host falls back to its own wording
  });
});

describe("the RNGs are two different generators", () => {
  // Granular-Synthesis.md gives this one exactly, so it is testable.
  it("reproduces the ANSI-C LCG byte-exactly", () => {
    const r = new AnsiCRandom(1);
    // seed = 1*0x41C64E6D + 0x3039 = 0x41C6811E -> (>>16)&0x7FFF
    const expectedSeed = (Math.imul(1, 0x41c64e6d) + 0x3039) >>> 0;
    expect(r.next()).toBe((expectedSeed >>> 16) & 0x7fff);
  });

  it("keeps the ANSI-C LCG inside RAND_MAX", () => {
    const r = new AnsiCRandom(12345);
    for (let i = 0; i < 500; i++) {
      const v = r.next();
      expect(v).toBeGreaterThanOrEqual(0);
      expect(v).toBeLessThanOrEqual(AnsiCRandom.RAND_MAX);
    }
  });

  it("is exactly reproducible from a seed — grain jitter must be", () => {
    const a = new AnsiCRandom(42);
    const b = new AnsiCRandom(42);
    for (let i = 0; i < 50; i++) expect(a.next()).toBe(b.next());
  });

  it("keeps the Toolbox trap in signed-word range", () => {
    const r = new ToolboxRandom(1);
    for (let i = 0; i < 500; i++) {
      const v = r.next();
      expect(v).toBeGreaterThanOrEqual(-32768);
      expect(v).toBeLessThanOrEqual(32767);
    }
  });

  it("gives the two generators different streams", () => {
    const toolbox = new ToolboxRandom(1);
    const ansi = new AnsiCRandom(1);
    const a = [toolbox.next(), toolbox.next(), toolbox.next()];
    const b = [ansi.next(), ansi.next(), ansi.next()];
    expect(a).not.toEqual(b);
  });
});

describe("the settings schema", () => {
  it("rejects a unit-dependent field with no units popup", () => {
    // ModSetups.h: "There MUST be a type_unitsPopup item if one or more of these
    // is present."
    const errors = validateSchema([
      { type: ItemType.editFieldUnitDependent, key: "delay" },
    ]);
    expect(errors).toHaveLength(1);
    expect(errors[0]).toMatch(/unitsPopup/);
  });

  it("accepts one with the popup present", () => {
    expect(
      validateSchema([
        { type: ItemType.editFieldUnitDependent, key: "delay" },
        { type: ItemType.unitsPopup, key: "units" },
      ]),
    ).toEqual([]);
  });

  it("omits NoSave items from a captured setup", () => {
    const schema = [
      { type: ItemType.checkBox, key: "kept" } as const,
      { type: ItemType.checkBoxNoSave, key: "dropped" } as const,
    ];
    const setup = captureSetup(schema, { kept: 1, dropped: 1 }, "mine");
    expect(setup.values).toEqual({ kept: 1 });
    expect(setup.name).toBe("mine");
  });

  it("applies a setup over current values", () => {
    const setup = { name: "x", values: { a: 2 } };
    expect(applySetup(setup, { a: 1, b: 9 })).toEqual({ a: 2, b: 9 });
  });
});

describe("host progress", () => {
  it("reports progress and closes", () => {
    const seen: Array<[number, number]> = [];
    const host = new Host({ onProgress: (cur, total) => (seen.push([cur, total]), false) });
    host.setupProgress("Testing…");
    expect(host.timeToCallProgress).toBe(true);
    host.showProgress(1, 10, false);
    host.closeProgress();
    expect(seen).toEqual([[1, 10]]);
    expect(host.isProgressOpen).toBe(false);
  });

  it("honours a cancel when canCancel is true", () => {
    const host = new Host({ onProgress: () => true });
    host.setupProgress("");
    expect(host.showProgress(1, 10, true)).not.toBe(kModNoError);
  });

  // Amplify, Echo, Delay, Noise, Smooth, Robotize all hard-code canCancel=false.
  it("ignores a cancel when canCancel is false — a long Echo is uninterruptible", () => {
    const host = new Host({ onProgress: () => true });
    host.setupProgress("");
    expect(host.showProgress(1, 10, false)).toBe(kModNoError);
  });
});

describe("host doExtend", () => {
  it("uses the pref directly when it is not 'ask'", () => {
    const never = new Host({ askDoExtend: () => true });
    never.doExtend = DoExtend.never;
    expect(never.askDoExtend()).toBe(false); // the answerer is not consulted

    const always = new Host();
    always.doExtend = DoExtend.always;
    expect(always.askDoExtend()).toBe(true);
  });

  it("asks live when the pref is 'Ask Each Time'", () => {
    let asked = 0;
    const host = new Host({ askDoExtend: () => (asked++, true) });
    host.doExtend = DoExtend.ask;
    expect(host.askDoExtend()).toBe(true);
    expect(asked).toBe(1);
  });

  it("declines to extend when nobody is there to ask", () => {
    const host = new Host();
    host.doExtend = DoExtend.ask;
    expect(host.askDoExtend()).toBe(false);
  });
});

describe("host settings persistence", () => {
  it("stores and loads by signature", () => {
    const host = new Host();
    expect(host.loadEffectSettings("Sig")).toBeNull();
    host.saveEffectSettings("Sig", { gain: 300 });
    expect(host.loadEffectSettings("Sig")).toEqual({ gain: 300 });
  });

  it("keeps named setups per signature", () => {
    const host = new Host();
    host.setSetup("Flanger", 0, { delay: 11 }, "Plain Flange");
    expect(host.getSetupsNumber("Flanger")).toBe(1);
    expect(host.getSetupName("Flanger", 0)).toBe("Plain Flange");
    expect(host.getSetup("Flanger", 0)).toEqual({ delay: 11 });
    host.deleteSetup("Flanger", 0);
    expect(host.getSetupsNumber("Flanger")).toBe(0);
  });
});

describe("message dispatch", () => {
  const stub: EffectModule<{ n: number }> = {
    name: "Stub",
    signature: "Stub",
    defaults: () => ({ n: 1 }),
    schema: () => [{ type: ItemType.editFieldNumerical, key: "n" }],
    effect: () => kModNoError,
  };

  it("answers Mhvr with the headers version", () => {
    expect(dispatch(stub, Msg.headersVers, new SoundDocument(), new Host())).toBe(
      kHeadersVersion,
    );
  });

  it("rejects an unknown message", () => {
    expect(
      dispatch(stub, "Xxxx" as never, new SoundDocument(), new Host()),
    ).toBe(kModUnknownMessage);
  });

  it("Mrun uses the persisted blob — that IS run-with-last-settings", () => {
    const host = new Host();
    host.saveEffectSettings("Stub", { n: 42 });
    let seen = 0;
    const mod = { ...stub, effect: (_d: SoundDocument, _h: Host, p: { n: number }) => (seen = p.n, kModNoError) };
    dispatch(mod, Msg.run, new SoundDocument(), host);
    expect(seen).toBe(42);
  });

  it("Msar runs with exactly what the dialog produced, and persists it", () => {
    const host = new Host();
    let seen = 0;
    const mod = { ...stub, effect: (_d: SoundDocument, _h: Host, p: { n: number }) => (seen = p.n, kModNoError) };
    dispatch(mod, Msg.settingsRun, new SoundDocument(), host, () => ({ n: 7 }) as never);
    expect(seen).toBe(7);
    expect(host.loadEffectSettings("Stub")).toEqual({ n: 7 });
  });

  it("Msar cancels without running", () => {
    let ran = false;
    const mod = { ...stub, effect: () => ((ran = true), kModNoError) };
    const r = dispatch(mod, Msg.settingsRun, new SoundDocument(), new Host(), () => null);
    expect(r).toBe(kModCancel);
    expect(ran).toBe(false);
  });

  it("a module with no schema says so — 'Sorry, man. I haven't got any options.'", () => {
    const bare: EffectModule = { name: "Bare", signature: "Bare", effect: () => kModNoError };
    expect(dispatch(bare, Msg.settings, new SoundDocument(), new Host())).toBe(
      kModHasNoSettings,
    );
  });

  it("but Msar still runs a no-options module", () => {
    let ran = false;
    const bare: EffectModule = {
      name: "Bare",
      signature: "Bare",
      effect: () => ((ran = true), kModNoError),
    };
    dispatch(bare, Msg.settingsRun, new SoundDocument(), new Host());
    expect(ran).toBe(true);
  });
});

describe("ModResampleSoundRate — drop-sample / zero-order-hold", () => {
  const doc8 = (samples: number[], rate: number) => {
    const d = new SoundDocument({ bps: 8, numChans: 1, rate: doubleToFixed(rate) });
    d.channels[0]!.data = Uint8Array.from(samples.map((s) => (s ^ 0x80) & 0xff));
    return d;
  };
  const read = (d: SoundDocument) =>
    Array.from(d.channels[0]!.data).map((b) => ((b ^ 0x80) << 24) >> 24);

  it("duplicates samples when upsampling — it does not interpolate", () => {
    const d = doc8([10, 20], 1000);
    modResampleSoundRate(d, doubleToFixed(2000));
    // ZOH: each source sample held across two outputs. Linear interpolation
    // would have produced a 15 in the middle; there is none.
    expect(read(d)).toEqual([10, 10, 20, 20]);
    expect(read(d)).not.toContain(15);
  });

  it("drops samples when downsampling", () => {
    const d = doc8([10, 20, 30, 40], 1000);
    modResampleSoundRate(d, doubleToFixed(500));
    expect(read(d)).toEqual([10, 30]);
  });

  it("updates the document's rate", () => {
    const d = doc8([1, 2], 1000);
    modResampleSoundRate(d, doubleToFixed(2000));
    expect(d.rate).toBe(doubleToFixed(2000));
  });

  it("clamps the target rate at 1.0 Hz", () => {
    const d = doc8([1, 2], 1000);
    modResampleSoundRate(d, 0);
    expect(d.rate).toBe(0x10000); // 1.0 in Fixed 16.16
  });

  it("resamples every channel of the document", () => {
    const d = doc8([10, 20], 1000);
    d.addChannel();
    d.channels[1]!.data = Uint8Array.from([30, 40].map((s) => (s ^ 0x80) & 0xff));
    modResampleSoundRate(d, doubleToFixed(2000));
    expect(d.channels[0]!.data.length).toBe(4);
    expect(d.channels[1]!.data.length).toBe(4);
  });
});

describe("ModResampleSoundBits — truncate / zero-extend, never round", () => {
  it("narrows 16→8 by keeping the top byte, re-offsetting it", () => {
    const d = new SoundDocument({ bps: 16, numChans: 1 });
    d.channels[0]!.data = new Uint8Array(4);
    setSample(d.channels[0]!.data, 0, 0x1234, 16);
    setSample(d.channels[0]!.data, 2, -0x1234, 16);
    modResampleSoundBits(d, 8);
    expect(d.bps).toBe(8);
    // Top byte kept: 0x1234 >> 8 = 0x12. Truncation, not rounding to 0x12+1.
    expect(getSample(d.channels[0]!.data, 0, 8)).toBe(0x12);
    expect(getSample(d.channels[0]!.data, 1, 8)).toBe(-0x13); // -0x1234 >> 8
  });

  it("truncates rather than rounding — 0x12FF must not become 0x13", () => {
    const d = new SoundDocument({ bps: 16, numChans: 1 });
    d.channels[0]!.data = new Uint8Array(2);
    setSample(d.channels[0]!.data, 0, 0x12ff, 16);
    modResampleSoundBits(d, 8);
    expect(getSample(d.channels[0]!.data, 0, 8)).toBe(0x12);
  });

  it("masks off low bits when the byte count is unchanged (16→12)", () => {
    const d = new SoundDocument({ bps: 16, numChans: 1 });
    d.channels[0]!.data = Uint8Array.from([0xff, 0xff]);
    modResampleSoundBits(d, 12);
    expect(d.bps).toBe(12);
    expect(d.channels[0]!.data.length).toBe(2); // size unchanged
    expect(Array.from(d.channels[0]!.data)).toEqual([0xff, 0xf0]); // low 4 bits gone
  });

  it("widens 8→16 by shifting left and zero-padding", () => {
    const d = new SoundDocument({ bps: 8, numChans: 1 });
    d.channels[0]!.data = Uint8Array.from([(0x12 ^ 0x80) & 0xff]);
    modResampleSoundBits(d, 16);
    expect(d.bps).toBe(16);
    expect(Array.from(d.channels[0]!.data)).toEqual([0x12, 0x00]);
    expect(getSample(d.channels[0]!.data, 0, 16)).toBe(0x1200);
  });

  it("adds no dither of its own — that is the module's job", () => {
    const d = new SoundDocument({ bps: 16, numChans: 1 });
    d.channels[0]!.data = new Uint8Array(8); // digital silence
    modResampleSoundBits(d, 8);
    // Silence in, silence out. A dithering host would have added noise here.
    expect(Array.from(d.channels[0]!.data)).toEqual([0x80, 0x80, 0x80, 0x80]);
  });
});
