#include "Wav.h"

#include "SampleFormat.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace se {

namespace {

void put32 (std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back ((uint8_t) (v >> (8 * i))); }
void put16 (std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 2; ++i) o.push_back ((uint8_t) (v >> (8 * i))); }
void tag (std::vector<uint8_t>& o, const char* t) { o.insert (o.end(), t, t + 4); }

uint32_t get32 (const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }
uint32_t get16 (const uint8_t* p) { return p[0] | (p[1] << 8); }

/** Reverse the byte order of each whole frame (big-endian <-> little-endian). 8-bit
    frames are the same either way. A trailing partial frame is left as it is. */
void swapFrames (uint8_t* d, size_t bytes, int fs)
{
    if (fs < 2) return;
    for (size_t o = 0; o + (size_t) fs <= bytes; o += (size_t) fs)
        std::reverse (d + o, d + o + fs);
}

} // namespace

void encodeTrackWav (const Track& track, double rateHz, int bps, std::vector<uint8_t>& out)
{
    const int fs = std::max (1, frameSizeFor (bps));
    const size_t n = track.data.size();
    const uint32_t rate = (uint32_t) std::max (1.0, std::min (4294967295.0, std::round (rateHz)));
    const size_t pad = n & 1;

    out.clear();
    out.reserve (44 + n + pad);
    tag (out, "RIFF");
    put32 (out, (uint32_t) (36 + n + pad));
    tag (out, "WAVE");
    tag (out, "fmt ");
    put32 (out, 16);
    put16 (out, 1);                    // integer PCM
    put16 (out, 1);                    // mono
    put32 (out, rate);
    put32 (out, rate * (uint32_t) fs);
    put16 (out, (uint32_t) fs);
    put16 (out, (uint32_t) fs * 8);
    tag (out, "data");
    put32 (out, (uint32_t) n);

    out.insert (out.end(), track.data.begin(), track.data.end());
    swapFrames (out.data() + 44, n, fs);
    if (pad) out.push_back (0);
}

bool decodeTrackWav (const uint8_t* wav, size_t size, int bps, uint64_t expectedBytes,
                     std::vector<uint8_t>& data)
{
    if (wav == nullptr || size < 12 || std::memcmp (wav, "RIFF", 4) != 0
        || std::memcmp (wav + 8, "WAVE", 4) != 0)
        return false;

    const int fs = std::max (1, frameSizeFor (bps));
    bool haveFmt = false;
    size_t pos = 12;

    while (pos + 8 <= size)
    {
        const uint32_t len = get32 (wav + pos + 4);
        const size_t body = pos + 8;
        if ((uint64_t) body + len > size) return false;

        if (std::memcmp (wav + pos, "fmt ", 4) == 0)
        {
            if (len < 16) return false;
            const uint32_t format = get16 (wav + body);
            haveFmt = (format == 1 || format == 0xFFFE)
                   && get16 (wav + body + 2) == 1
                   && get16 (wav + body + 14) == (uint32_t) fs * 8;
            if (! haveFmt) return false;
        }
        else if (std::memcmp (wav + pos, "data", 4) == 0)
        {
            if (! haveFmt || len != expectedBytes) return false;
            std::vector<uint8_t> d (wav + body, wav + body + len);
            swapFrames (d.data(), d.size(), fs);
            data = std::move(d);
            return true;
        }

        pos = body + len + (len & 1);
    }
    return false;
}

} // namespace se
