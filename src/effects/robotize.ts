/**
 * **Robotize** — SPEC §8 #27. `[BINARY: disassembly/Robotize.md]`
 *
 *     y[n] = clamp( x[n] + trunc( y[n−D] × 75 / 100 ) )
 *     D    = trunc( 190 × rate / 22254.5454… )  samples
 *
 * A recursive feedback comb at a fixed ~117 Hz. The 190 is samples *at the Mac's
 * native 22.254 kHz rate*, rescaled to the document's — so the delay is a
 * constant ≈8.54 ms at any rate, and the comb fundamental is
 * 22254.5454/190 ≈ **117.13 Hz**. That pitch is the robot's voice; it does not
 * track the sound, which is exactly why it sounds like a machine.
 */

import { frameSizeFor, getSample, setSample } from "../samples.js";
import { runSkeleton } from "../runtime/skeleton.js";
import { kModNoError } from "../runtime/results.js";
import { fixedToDouble } from "../units.js";
import type { EffectModule } from "../runtime/module.js";

/**
 * Fixed 0x56EE8BA3 = 22254 6/11 Hz — the classic Mac "Standard" rate, read
 * straight from the binary.
 *
 * Exported because the truncation below makes it sharper than it looks: a rate
 * one 1/65536th *below* this gives `trunc(190 × 0.99999…)` = **189**, not 190.
 * Anything reasoning about Robotize's delay wants the exact constant, not a
 * decimal approximation of it.
 */
export const NATIVE_RATE = 0x56ee8ba3;
/** Word at code offset 0x8d2. */
const BASE_DELAY_SAMPLES = 190;
/** The feedback amount, integer percent. Not a parameter — Robotize has none. */
const FEEDBACK_PERCENT = 75;

export const Robotize: EffectModule = {
  name: "Robotize",
  signature: "Robotize",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Robotizing sound…",

  effect(doc, host) {
    const frameSize = frameSizeFor(doc.bps);
    const { min, max } = host.getSampleValueLimits(doc.bps);

    // Computed once per run. The original goes through SANE extended precision
    // and truncates to integral; at exactly the native rate the division cancels
    // and D is 190 on the nose.
    const D = Math.trunc(
      (BASE_DELAY_SAMPLES * fixedToDouble(doc.rate)) / fixedToDouble(NATIVE_RATE),
    );
    const delayBytes = D * frameSize;

    return runSkeleton(doc, host, { text: "Robotizing sound…", canCancel: false }, (pass, progress) => {
      // Selections shorter than the delay (~8.5 ms) are a silent no-op — the
      // loop simply never runs. The first D samples are the untouched seed.
      for (let i = pass.start; i < pass.end - delayBytes; i += frameSize) {
        // Reads the CURRENT, possibly already-modified value: this forward pass
        // is genuinely recursive, and one pass produces the whole comb.
        const here = getSample(pass.data, i, doc.bps);
        const target = i + delayBytes;
        let ahead = getSample(pass.data, target, doc.bps);

        ahead += Math.trunc((here * FEEDBACK_PERCENT) / 100);
        if (ahead < min) ahead = min;
        else if (ahead > max) ahead = max;

        setSample(pass.data, target, ahead, doc.bps);
        if (progress.tick()) return kModNoError;
      }
    });
  },
};
