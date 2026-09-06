#pragma once
// Fixed-bucket latency histogram (microseconds). No allocation after
// construction, cheap to record on the broadcaster thread, cheap to snapshot.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace mm {

class LatencyStats {
public:
    static constexpr uint32_t kBucketUs = 25;      // resolution
    static constexpr std::size_t kBuckets = 2048;  // 2048 * 25us = 51.2 ms, then overflow

    void record(uint32_t us) noexcept {
        std::size_t b = us / kBucketUs;
        if (b >= kBuckets) b = kBuckets - 1;
        ++buckets_[b];
        ++count_;
        sum_ += us;
        min_ = std::min(min_, us);
        max_ = std::max(max_, us);
    }

    uint64_t count() const noexcept { return count_; }
    uint32_t min_us() const noexcept { return count_ ? min_ : 0; }
    uint32_t max_us() const noexcept { return max_; }
    uint32_t mean_us() const noexcept { return count_ ? static_cast<uint32_t>(sum_ / count_) : 0; }

    // Upper edge of the bucket containing the requested percentile (0..100).
    uint32_t percentile_us(double pct) const noexcept {
        if (count_ == 0) return 0;
        const double target = std::clamp(pct, 0.0, 100.0) / 100.0 * static_cast<double>(count_);
        uint64_t seen = 0;
        for (std::size_t b = 0; b < kBuckets; ++b) {
            seen += buckets_[b];
            if (static_cast<double>(seen) >= target) return static_cast<uint32_t>((b + 1) * kBucketUs);
        }
        return kBuckets * kBucketUs;
    }

    void reset() noexcept {
        buckets_.fill(0);
        count_ = 0;
        sum_ = 0;
        min_ = UINT32_MAX;
        max_ = 0;
    }

private:
    std::array<uint64_t, kBuckets> buckets_{};
    uint64_t count_ = 0;
    uint64_t sum_ = 0;
    uint32_t min_ = UINT32_MAX;
    uint32_t max_ = 0;
};

}  // namespace mm
