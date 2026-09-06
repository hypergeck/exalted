#pragma once
// Lock-free single-producer / single-consumer ring buffer.
// Producer: the low-level hook thread. Consumer: the broadcaster thread.
// No allocation, no locks, no syscalls. Head and tail sit on separate cache
// lines so the two threads never contend on the same line.
#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace mm {

template <class T, std::size_t Capacity>
class SpscRing {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>, "ring elements are copied by value");

public:
    static constexpr std::size_t capacity() noexcept { return Capacity; }

    // Producer side. Returns false (and counts a drop) when the ring is full.
    bool push(const T& value) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail == Capacity) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        buf_[head & kMask] = value;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns false when the ring is empty.
    bool pop(T& out) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (head == tail) return false;
        out = buf_[tail & kMask];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    std::size_t size() const noexcept {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }
    bool empty() const noexcept { return size() == 0; }
    std::size_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    static constexpr std::size_t kMask = Capacity - 1;
    static constexpr std::size_t kLine = 64;

    alignas(kLine) std::atomic<std::size_t> head_{0};
    alignas(kLine) std::atomic<std::size_t> tail_{0};
    alignas(kLine) std::atomic<std::size_t> dropped_{0};
    alignas(kLine) std::array<T, Capacity> buf_{};
};

}  // namespace mm
