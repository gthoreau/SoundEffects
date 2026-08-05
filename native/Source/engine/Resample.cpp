#include "Resample.h"

#include "SampleFormat.h"

#include <algorithm>
#include <cmath>

namespace se {

void resampleSoundBits (SoundDocument& doc, int newBps)
{
    const int oldBps = doc.bps;
    const int oldFrameSize = frameSizeFor (oldBps);
    const int newFrameSize = frameSizeFor (newBps);

    if (newFrameSize < oldFrameSize)
    {
        // Narrowing the byte count: keep the most-significant bytes.
        for (auto& t : doc.tracks)
        {
            const auto& src = t.data;
            const size_t frames = src.size() / static_cast<size_t> (oldFrameSize);
            std::vector<uint8_t> dst (frames * static_cast<size_t> (newFrameSize));
            for (size_t f = 0; f < frames; ++f)
            {
                const size_t s = f * static_cast<size_t> (oldFrameSize);
                const size_t d = f * static_cast<size_t> (newFrameSize);
                for (int b = 0; b < newFrameSize; ++b)
                    dst[d + static_cast<size_t> (b)] = src[s + static_cast<size_t> (b)];
                if (newFrameSize == 1) dst[d] = static_cast<uint8_t> ((dst[d] ^ 0x80) & 0xff);
            }
            t.data = std::move (dst);
        }
    }
    else if (newFrameSize == oldFrameSize && newBps < oldBps)
    {
        // Same byte count, fewer bits: mask off the low bits in place.
        const int drop = oldBps - newBps;
        for (auto& t : doc.tracks)
        {
            const size_t frames = t.data.size() / static_cast<size_t> (oldFrameSize);
            for (size_t f = 0; f < frames; ++f)
            {
                const size_t off = f * static_cast<size_t> (oldFrameSize);
                int remaining = drop;
                for (int b = oldFrameSize - 1; b >= 0 && remaining > 0; --b)
                {
                    const int bitsHere = std::min (8, remaining);
                    t.data[off + static_cast<size_t> (b)] =
                        static_cast<uint8_t> ((t.data[off + static_cast<size_t> (b)] & (0xff << bitsHere)) & 0xff);
                    remaining -= bitsHere;
                }
            }
        }
    }
    else if (newFrameSize > oldFrameSize)
    {
        // Widening: shift left (old bytes to the high end), zero-pad the low bytes.
        for (auto& t : doc.tracks)
        {
            const auto& src = t.data;
            const size_t frames = src.size() / static_cast<size_t> (oldFrameSize);
            std::vector<uint8_t> dst (frames * static_cast<size_t> (newFrameSize), 0);
            for (size_t f = 0; f < frames; ++f)
            {
                const size_t s = f * static_cast<size_t> (oldFrameSize);
                const size_t d = f * static_cast<size_t> (newFrameSize);
                for (int b = 0; b < oldFrameSize; ++b)
                {
                    uint8_t byte = src[s + static_cast<size_t> (b)];
                    if (oldFrameSize == 1) byte = static_cast<uint8_t> ((byte ^ 0x80) & 0xff);   // un-offset first
                    dst[d + static_cast<size_t> (b)] = byte;
                }
            }
            t.data = std::move (dst);
        }
    }
    // newFrameSize == oldFrameSize && newBps >= oldBps: nothing to do.

    doc.bps = newBps;
    doc.selSt = 0; doc.selEnd = 0;   // a frame-size change invalidates byte offsets
}

void resampleSoundRate (SoundDocument& doc, double newRateHz)
{
    const double newRate = std::max (newRateHz, 1.0);
    const int oldFrameSize = frameSizeFor (doc.bps);
    const double oldRate = doc.rateHz;
    const double step = (oldFrameSize * oldRate) / newRate;   // source bytes per output sample

    for (auto& t : doc.tracks)
    {
        const auto& src = t.data;
        const size_t oldSize = src.size();
        long long newSize = std::llround ((double) oldSize * newRate / oldRate);
        newSize -= newSize % oldFrameSize;
        if (newSize < 0) newSize = 0;
        std::vector<uint8_t> dst (static_cast<size_t> (newSize));

        double pos = 0.0;
        for (long long out = 0; out + oldFrameSize <= newSize; out += oldFrameSize)
        {
            long long idx = static_cast<long long> (pos);   // trunc toward zero
            idx -= idx % oldFrameSize;
            if (idx + oldFrameSize <= static_cast<long long> (oldSize))
                for (int b = 0; b < oldFrameSize; ++b)
                    dst[static_cast<size_t> (out + b)] = src[static_cast<size_t> (idx + b)];
            pos += step;
        }
        t.data = std::move (dst);
    }

    doc.rateHz = newRate;
    doc.selSt = 0; doc.selEnd = 0;
}

} // namespace se
