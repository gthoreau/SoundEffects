// SampleFormat — packed-byte sample access, ported byte-for-byte from the TS
// reference `src/samples.ts` (itself `[SOURCE]` from the shipped Developer's Kit
// `ModGetSetSamples.{h,c}`). No JUCE here: this is pure integer DSP plumbing.
//
// The integer path is the reason the effects sound the way they do, so this is a
// transliteration, not a tidy-up. 8-bit and under is offset-binary
// (signed = stored ^ 0x80); 9..32 bits are big-endian, right-aligned with sign
// extension.

#pragma once

#include <cstddef>
#include <cstdint>

namespace se {

/** frameSize = bps/8 + (bps%8 != 0) — bytes per sample. */
inline int frameSizeFor (int bps) noexcept
{
    return bps / 8 + (bps % 8 != 0 ? 1 : 0);
}

/** Bytes of the frame that actually carry the value: min(frameSize, 4). */
inline int valueBytes (int bps) noexcept
{
    const int size = frameSizeFor (bps);
    return size < 4 ? size : 4;
}

/** Read one sample. `[SOURCE: GetSample8 / GetSample16]` */
inline int32_t getSample (const uint8_t* data, size_t offset, int bps) noexcept
{
    const int n = valueBytes (bps);
    if (n == 1)
    {
        // GetSample8: (signed char)(stored ^ 0x80)
        return static_cast<int8_t> (data[offset] ^ 0x80);
    }

    int32_t v = 0;
    for (int b = 0; b < n; ++b)
        v = (v << 8) | data[offset + static_cast<size_t> (b)];

    const int shift = (4 - n) * 8;     // sign-extend + right-align
    return (v << shift) >> shift;
}

/** Write one sample. `[SOURCE: SetSample8 / SetSample16]` */
inline void setSample (uint8_t* data, size_t offset, int32_t value, int bps) noexcept
{
    const int n = valueBytes (bps);
    if (n == 1)
    {
        // SetSample8: stored = (SignedByte)(s) ^ 0x80
        data[offset] = static_cast<uint8_t> ((value ^ 0x80) & 0xff);
        return;
    }

    for (int b = 0; b < n; ++b)
        data[offset + static_cast<size_t> (b)] =
            static_cast<uint8_t> ((static_cast<uint32_t> (value) >> ((n - 1 - b) * 8)) & 0xff);
}

/** The byte value that reads as zero at this depth: 0x80 for 8-bit, 0x00 above. */
inline uint8_t silentByte (int bps) noexcept
{
    return frameSizeFor (bps) == 1 ? uint8_t (0x80) : uint8_t (0x00);
}

/** Saturating clamp — the "hard clip" every shipped effect ends with. */
inline int32_t clampSample (int32_t value, int32_t lo, int32_t hi) noexcept
{
    return value < lo ? lo : (value > hi ? hi : value);
}

} // namespace se
