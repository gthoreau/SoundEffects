// TapeStore — the Magnetophon's audio: stereo float frames at the device rate, in
// fixed-size chunks the MESSAGE thread allocates as the tape grows and the AUDIO
// thread reads while the tape plays. No JUCE.
//
// Threading:
//   - reserve() / write() / setLength() / release(): message thread only.
//   - read(): the audio thread (tape playback), or the message thread.
//   - The chunk table is allocated once, so a chunk pointer never moves; a chunk is
//     published (release store) before the length that covers it, and the audio
//     thread reads only below the length it acquired. So it never sees a chunk
//     half-made.
//   - Recording and playing are exclusive (the TapeDeck stops playback before a
//     take starts), and the frames the message thread writes come out of a capture
//     ring the audio thread filled AFTER it stopped playing, so record-over never
//     writes a frame the audio thread is reading.
//   - release() frees the chunks: only once the audio thread has confirmed it is not
//     playing (Engine::tapePlaying() false for the latest command), which the
//     TapeDeck waits for.
//   - The fade-out of a stop or a jump is copied aside as the command is applied
//     (Engine::fadeOutTape), so it reads nothing once that command is acknowledged,
//     and nothing a following take could be writing.

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>

namespace se {

class TapeStore
{
public:
    /** 65 536 frames a chunk (1.4 s at 48 kHz, 512 KB of stereo float). */
    static constexpr size_t kChunkFrames = size_t (1) << 16;
    /** 4096 chunks: 93 minutes at 48 kHz, 46 at 96 kHz. The tape stops there. */
    static constexpr size_t kMaxChunks = 4096;
    static constexpr size_t kMaxFrames = kChunkFrames * kMaxChunks;

    TapeStore() : chunks (new std::atomic<float*>[kMaxChunks])
    {
        for (size_t i = 0; i < kMaxChunks; ++i) chunks[i].store (nullptr, std::memory_order_relaxed);
    }
    ~TapeStore() { release(); }

    TapeStore (const TapeStore&) = delete;
    TapeStore& operator= (const TapeStore&) = delete;

    // --- message thread ------------------------------------------------------
    /** Make room for `frames` frames. False (nothing more allocated) past kMaxFrames. */
    bool reserve (size_t frames)
    {
        if (frames > kMaxFrames) return false;
        const size_t need = (frames + kChunkFrames - 1) / kChunkFrames;
        for (; allocated < need; ++allocated)
            chunks[allocated].store (new float[kChunkFrames * 2](), std::memory_order_release);
        return true;
    }

    /** Write `n` frames at `at` into reserved room; returns how many fitted. Does not
        publish: setLength() does. */
    size_t write (size_t at, const float* l, const float* r, size_t n) noexcept
    {
        const size_t room = allocated * kChunkFrames;
        if (at >= room) return 0;
        n = std::min (n, room - at);
        size_t done = 0;
        while (done < n)
        {
            const size_t f = at + done;
            float* c = chunks[f / kChunkFrames].load (std::memory_order_relaxed);
            const size_t off = f % kChunkFrames;
            const size_t k = std::min (n - done, kChunkFrames - off);
            std::copy (l + done, l + done + k, c + off);
            std::copy (r + done, r + done + k, c + kChunkFrames + off);
            done += k;
        }
        return n;
    }

    /** Publish how many frames the tape holds (never past what is reserved). */
    void setLength (size_t n) noexcept
    {
        len.store (std::min (n, allocated * kChunkFrames), std::memory_order_release);
    }

    /** Free every chunk; the length goes to 0. See the threading note above. */
    void release() noexcept
    {
        len.store (0, std::memory_order_release);
        for (size_t i = 0; i < allocated; ++i)
            delete[] chunks[i].exchange (nullptr, std::memory_order_acq_rel);
        allocated = 0;
    }

    /** Chunks allocated (message thread). */
    size_t reservedFrames() const noexcept { return allocated * kChunkFrames; }

    // --- any thread ----------------------------------------------------------
    size_t length() const noexcept { return len.load (std::memory_order_acquire); }

    /** Copy up to `n` frames from `at`, stopping at the length; returns how many. */
    size_t read (size_t at, float* l, float* r, size_t n) const noexcept
    {
        const size_t end = length();
        if (at >= end) return 0;
        n = std::min (n, end - at);
        size_t done = 0;
        while (done < n)
        {
            const size_t f = at + done;
            const float* c = chunks[f / kChunkFrames].load (std::memory_order_acquire);
            const size_t off = f % kChunkFrames;
            const size_t k = std::min (n - done, kChunkFrames - off);
            std::copy (c + off, c + off + k, l + done);
            std::copy (c + kChunkFrames + off, c + kChunkFrames + off + k, r + done);
            done += k;
        }
        return n;
    }

    /** One frame, below the length (the scrub reads frame by frame). */
    void frameAt (size_t f, float& l, float& r) const noexcept
    {
        const float* c = chunks[f / kChunkFrames].load (std::memory_order_acquire);
        const size_t off = f % kChunkFrames;
        l = c[off];
        r = c[kChunkFrames + off];
    }

private:
    std::unique_ptr<std::atomic<float*>[]> chunks;   // each: kChunkFrames left, then right
    size_t allocated = 0;                            // message thread
    std::atomic<size_t> len { 0 };
};

} // namespace se
