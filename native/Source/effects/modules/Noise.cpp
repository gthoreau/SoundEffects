// Noise — SPEC §8 #21. [BINARY: disassembly/Noise.md]
// Ported from `src/effects/noise.ts`.
//
// "makes some noise. That's all there is to it."
//
// The one module that ignores the sample model. It REPLACES the selection with
// Random() output written 16 BITS AT A TIME REGARDLESS OF bps, with single-byte
// fixups if the range starts or ends on an odd address. No frameSize stepping, no
// XOR 0x80, no sign handling — a raw word store.
//
// That is not sloppiness, it is the one place where it is provably safe: random
// bytes are random bytes however you later group them into samples. The only
// correctness requirement is not writing past the selection, which the odd-byte
// fixups handle at any depth. Writing words instead of bytes halves the trap calls
// for identical output.

#include "Modules.h"

#include "../Skeleton.h"

namespace se::modules {

EffectModule makeNoise()
{
    EffectModule m;
    m.name = "Noise";
    m.signature = "Noise";
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Makin' noise…";

    m.effect = [] (SoundDocument& doc, Host& host, const Settings&) -> ModResult
    {
        return runSkeleton (doc, host, { "Makin' noise…", false },
                            [&host] (const TrackPass& pass, Progress& progress) -> ModResult
        {
            uint8_t* data = pass.data;
            size_t p = pass.start;

            // Leading byte fixup: if the start is odd, write one byte and move to
            // the next (even) address.
            if (p % 2 != 0 && p < pass.end)
            {
                data[p] = static_cast<uint8_t> (host.random.next() & 0xff);
                ++p;
            }

            // Main loop: step by 2, storing the full 16-bit result big-endian.
            // No read-modify-add — this replaces, where Dither adds.
            for (; p + 1 < pass.end; p += 2)
            {
                const int32_t r = host.random.next();
                data[p]     = static_cast<uint8_t> ((static_cast<uint32_t> (r) >> 8) & 0xff);
                data[p + 1] = static_cast<uint8_t> (r & 0xff);

                if (progress.tick()) return kModNoError;
            }

            // Trailing byte fixup: if the length's parity left one byte unwritten.
            if (p < pass.end)
                data[pass.end - 1] = static_cast<uint8_t> (host.random.next() & 0xff);

            return kModNoError;
        });
    };

    return m;
}

} // namespace se::modules
