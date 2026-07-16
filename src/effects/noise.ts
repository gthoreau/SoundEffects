/**
 * **Noise** — SPEC §8 #21. `[BINARY: disassembly/Noise.md]`
 *
 * "makes some noise. That's all there is to it."
 *
 * The one module that ignores the sample model. It **replaces** the selection
 * with `Random()` output written **16 bits at a time regardless of bps**, with
 * single-byte fixups if the range starts or ends on an odd address. No
 * `frameSize` stepping, no `XOR 0x80`, no sign handling — a raw word store.
 *
 * That is not sloppiness, it is the one place where it is provably safe: random
 * bytes are random bytes however you later group them into samples. The only
 * correctness requirement is not writing past the selection, which the odd-byte
 * fixups handle at any depth. Writing words instead of bytes halves the trap
 * calls for identical output.
 */

import { runSkeleton } from "../runtime/skeleton.js";
import { kModNoError } from "../runtime/results.js";
import type { EffectModule } from "../runtime/module.js";

export const Noise: EffectModule = {
  name: "Noise",
  signature: "Noise",
  version: "1.1",
  author: "Alberto Ricci",
  progressText: "Makin' noise…",

  effect(doc, host) {
    return runSkeleton(doc, host, { text: "Makin' noise…", canCancel: false }, (pass, progress) => {
      const { data } = pass;
      let p = pass.start;

      // Leading byte fixup: if the start is odd, write one byte and move to the
      // next (even) address.
      if (p % 2 !== 0 && p < pass.end) {
        data[p] = host.random.next() & 0xff;
        p++;
      }

      // Main loop: step by 2, storing the full 16-bit result big-endian. No
      // read-modify-add — this replaces, where Dither adds.
      for (; p + 1 < pass.end; p += 2) {
        const r = host.random.next();
        data[p] = (r >>> 8) & 0xff;
        data[p + 1] = r & 0xff;
        if (progress.tick()) return kModNoError;
      }

      // Trailing byte fixup: if the length's parity left one byte unwritten.
      if (p < pass.end) {
        data[pass.end - 1] = host.random.next() & 0xff;
      }
    });
  },
};
