#pragma once
#include <atomic>
#include <cstddef>
#include <vector>

// Single-producer single-consumer lock-free ring buffer of frames.
// Capacity must be a power of two. One acquisition thread pushes, the
// processing thread pops - no mutex on the hot path.
template <typename T>
class SpscRing {
public:
    explicit SpscRing(size_t capacity_pow2)
        : mask_(capacity_pow2 - 1), buf_(capacity_pow2) {
        // caller guarantees power of two
    }

    bool try_push(const T& v) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire)) return false;  // full
        buf_[head] = v;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool try_pop(T& out) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;  // empty
        out = buf_[tail];
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return true;
    }

private:
    const size_t mask_;
    std::vector<T> buf_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};
