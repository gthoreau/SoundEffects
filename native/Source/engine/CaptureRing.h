// CaptureRing — a lock-free, wait-free single-producer/single-consumer ring of
// stereo float frames: the audio thread pushes what the record source delivers,
// the message thread (the Recorder) pops it, converts it and writes it into a
// Sound. Block-wise (one release store per block), planar in, planar out.
//
// EXACTLY ONE thread may call push() and EXACTLY ONE (other) thread may call
// pop()/discard(). A push that does not fit is dropped whole-block-wise as far as it
// overflows and counted, never blocked on: the audio thread must not wait.

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

namespace se {

class CaptureRing
{
public:
    /** Frames of capacity (a power of two). 2^17 = ~2.7 s at 48 kHz: the Recorder
        drains it at 30 Hz, so this is generous, and it is allocated once. */
    explicit CaptureRing (size_t capacityFrames = size_t (1) << 17)
        : mask (capacityFrames - 1), left (capacityFrames), right (capacityFrames)
    {
        // Power of two: x & (x - 1) == 0.
        if ((capacityFrames & mask) != 0) { mask = 0; left.assign (1, 0.0f); right.assign (1, 0.0f); }
    }

    size_t capacity() const noexcept { return mask + 1; }

    /** Producer. Returns the frames that did not fit (they are lost, and counted). */
    size_t push (const float* l, const float* r, size_t n) noexcept
    {
        const size_t t = tail.load (std::memory_order_relaxed);
        const size_t h = head.load (std::memory_order_acquire);
        const size_t room = capacity() - (t - h) - 1;
        const size_t take = std::min (n, room);
        for (size_t i = 0; i < take; ++i)
        {
            left[(t + i) & mask]  = l[i];
            right[(t + i) & mask] = r[i];
        }
        tail.store (t + take, std::memory_order_release);
        if (take < n) dropped.fetch_add (n - take, std::memory_order_relaxed);
        return n - take;
    }

    /** Consumer. Copies up to `maxFrames` frames out; returns how many. */
    size_t pop (float* l, float* r, size_t maxFrames) noexcept
    {
        const size_t h = head.load (std::memory_order_relaxed);
        const size_t t = tail.load (std::memory_order_acquire);
        const size_t n = std::min (maxFrames, t - h);
        for (size_t i = 0; i < n; ++i)
        {
            l[i] = left[(h + i) & mask];
            r[i] = right[(h + i) & mask];
        }
        head.store (h + n, std::memory_order_release);
        return n;
    }

    /** Consumer. Throw away everything queued. */
    void discard() noexcept { head.store (tail.load (std::memory_order_acquire), std::memory_order_release); }

    size_t available() const noexcept
    {
        return tail.load (std::memory_order_acquire) - head.load (std::memory_order_acquire);
    }

    /** Frames lost to overflow since the last resetDropped(). */
    size_t droppedFrames() const noexcept { return dropped.load (std::memory_order_relaxed); }
    void resetDropped() noexcept { dropped.store (0, std::memory_order_relaxed); }

private:
    size_t mask;
    std::vector<float> left, right;
    alignas (64) std::atomic<size_t> head { 0 };   // consumer moves this
    alignas (64) std::atomic<size_t> tail { 0 };   // producer moves this
    alignas (64) std::atomic<size_t> dropped { 0 };
};

} // namespace se
