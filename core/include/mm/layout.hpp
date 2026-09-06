#pragma once
// Layout engine: turns a layout spec and a monitor work area into one Rect
// per slot, plus the instance->slot assignment that makes "swap master" a
// pure data operation. All integer math, no Win32.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "mm/geometry.hpp"

namespace mm {

enum class LayoutKind : uint8_t {
    Grid,     // rows x cols cells, slot 0 top-left, row-major
    Stacked,  // every slot is the whole work area (only the top window is visible; use previews)
    Pip,      // slot 0 fills the area minus a strip of thumbnails along one edge
};

enum class Edge : uint8_t { Right, Bottom, Left, Top };

struct LayoutSpec {
    LayoutKind kind = LayoutKind::Grid;
    int rows = 0;            // Grid: 0 = derive a near-square grid from the slot count
    int cols = 0;
    int thumb = 0;           // Pip: thumbnail strip thickness in pixels; 0 = 20% of the shorter side
    Edge edge = Edge::Right; // Pip: which edge holds the thumbnails
};

// Smallest rows x cols grid that fits n cells, favouring more columns than rows.
inline void derive_grid(std::size_t n, int& rows, int& cols) noexcept {
    if (n == 0) { rows = cols = 0; return; }
    cols = 1;
    while (static_cast<std::size_t>(cols) * static_cast<std::size_t>(cols) < n) ++cols;
    rows = static_cast<int>((n + static_cast<std::size_t>(cols) - 1) / static_cast<std::size_t>(cols));
}

// Returns exactly `slots` rectangles, one per slot index.
inline std::vector<Rect> compute_layout(const LayoutSpec& spec, const Rect& area, std::size_t slots) {
    std::vector<Rect> out(slots);
    if (slots == 0) return out;

    switch (spec.kind) {
    case LayoutKind::Stacked:
        std::fill(out.begin(), out.end(), area);
        return out;

    case LayoutKind::Grid: {
        int rows = spec.rows, cols = spec.cols;
        if (rows <= 0 || cols <= 0) derive_grid(slots, rows, cols);
        // Distribute remainder pixels so cells tile the area exactly.
        for (std::size_t i = 0; i < slots; ++i) {
            const int r = static_cast<int>(i) / cols;
            const int c = static_cast<int>(i) % cols;
            if (r >= rows) { out[i] = Rect{}; continue; }  // more slots than cells: park at zero size
            const int32_t x0 = area.x + static_cast<int32_t>(static_cast<int64_t>(area.w) * c / cols);
            const int32_t x1 = area.x + static_cast<int32_t>(static_cast<int64_t>(area.w) * (c + 1) / cols);
            const int32_t y0 = area.y + static_cast<int32_t>(static_cast<int64_t>(area.h) * r / rows);
            const int32_t y1 = area.y + static_cast<int32_t>(static_cast<int64_t>(area.h) * (r + 1) / rows);
            out[i] = Rect{x0, y0, x1 - x0, y1 - y0};
        }
        return out;
    }

    case LayoutKind::Pip: {
        const int shorter = std::min(area.w, area.h);
        const int thumb = spec.thumb > 0 ? spec.thumb : std::max(1, shorter / 5);
        Rect main = area;
        Rect strip = area;
        switch (spec.edge) {
        case Edge::Right:  main.w -= thumb; strip.x = main.right(); strip.w = thumb; break;
        case Edge::Left:   main.x += thumb; main.w -= thumb; strip.w = thumb; break;
        case Edge::Bottom: main.h -= thumb; strip.y = main.bottom(); strip.h = thumb; break;
        case Edge::Top:    main.y += thumb; main.h -= thumb; strip.h = thumb; break;
        }
        out[0] = main;
        const std::size_t thumbs = slots - 1;
        const bool vertical = spec.edge == Edge::Right || spec.edge == Edge::Left;
        for (std::size_t i = 1; i < slots; ++i) {
            const std::size_t k = i - 1;
            if (vertical) {
                const int32_t y0 = strip.y + static_cast<int32_t>(static_cast<int64_t>(strip.h) * k / thumbs);
                const int32_t y1 = strip.y + static_cast<int32_t>(static_cast<int64_t>(strip.h) * (k + 1) / thumbs);
                out[i] = Rect{strip.x, y0, strip.w, y1 - y0};
            } else {
                const int32_t x0 = strip.x + static_cast<int32_t>(static_cast<int64_t>(strip.w) * k / thumbs);
                const int32_t x1 = strip.x + static_cast<int32_t>(static_cast<int64_t>(strip.w) * (k + 1) / thumbs);
                out[i] = Rect{x0, strip.y, x1 - x0, strip.h};
            }
        }
        return out;
    }
    }
    return out;
}

// Which instance occupies which slot. Slot 0 is the master slot.
class Assignment {
public:
    explicit Assignment(std::size_t instances = 0) { reset(instances); }

    void reset(std::size_t instances) {
        slot_of_.resize(instances);
        for (std::size_t i = 0; i < instances; ++i) slot_of_[i] = static_cast<uint32_t>(i);
    }

    std::size_t size() const noexcept { return slot_of_.size(); }
    uint32_t slot_of(std::size_t instance) const noexcept { return slot_of_[instance]; }

    std::size_t instance_at(uint32_t slot) const noexcept {
        for (std::size_t i = 0; i < slot_of_.size(); ++i) if (slot_of_[i] == slot) return i;
        return static_cast<std::size_t>(-1);
    }

    std::size_t master() const noexcept { return instance_at(0); }

    void swap(std::size_t a, std::size_t b) noexcept {
        if (a < slot_of_.size() && b < slot_of_.size()) std::swap(slot_of_[a], slot_of_[b]);
    }

    // Moves `instance` into slot 0; the previous master takes its old slot.
    void set_master(std::size_t instance) noexcept {
        const std::size_t current = master();
        if (current != instance) swap(current, instance);
    }

    // Removes an instance and compacts slots so they stay 0..n-1.
    void erase(std::size_t instance) {
        if (instance >= slot_of_.size()) return;
        const uint32_t freed = slot_of_[instance];
        slot_of_.erase(slot_of_.begin() + static_cast<std::ptrdiff_t>(instance));
        for (auto& s : slot_of_) if (s > freed) --s;
    }

private:
    std::vector<uint32_t> slot_of_;
};

}  // namespace mm
