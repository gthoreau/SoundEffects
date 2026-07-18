// SpscQueue — a bounded, lock-free, wait-free single-producer/single-consumer
// ring buffer. Pure C++ (no JUCE), so the engine core stays self-contained and
// testable.
//
// Correctness contract: EXACTLY ONE thread may call push() and EXACTLY ONE
// (other) thread may call pop(). T must be trivially copyable (we carry PODs and
// raw pointers across the thread boundary — never anything that allocates on
// copy). Capacity must be a power of two; one slot is kept empty to disambiguate
// full from empty, so the usable depth is Capacity-1.

#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>

namespace se {

template <typename T, size_t Capacity>
class SpscQueue
{
public:
    static_assert ((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert (Capacity >= 2, "Capacity must be at least 2");
    static_assert (std::is_trivially_copyable<T>::value,
                   "T must be trivially copyable — no allocation may cross the thread boundary");

    /** Producer side. Returns false if the queue is full (caller decides what to do). */
    bool push (const T& value) noexcept
    {
        const size_t t = tail.load (std::memory_order_relaxed);
        const size_t next = (t + 1) & mask;
        if (next == head.load (std::memory_order_acquire))
            return false;                       // full
        buffer[t] = value;
        tail.store (next, std::memory_order_release);
        return true;
    }

    /** Consumer side. Returns false if the queue is empty. */
    bool pop (T& out) noexcept
    {
        const size_t h = head.load (std::memory_order_relaxed);
        if (h == tail.load (std::memory_order_acquire))
            return false;                       // empty
        out = buffer[h];
        head.store ((h + 1) & mask, std::memory_order_release);
        return true;
    }

private:
    static constexpr size_t mask = Capacity - 1;

    // Keep the two indices on separate cache lines to avoid false sharing between
    // producer and consumer.
    alignas (64) std::atomic<size_t> head { 0 };   // consumer moves this
    alignas (64) std::atomic<size_t> tail { 0 };   // producer moves this
    alignas (64) T buffer[Capacity] {};
};

} // namespace se
