#include "check.hpp"
#include "mm/geometry.hpp"
#include "mm/layout.hpp"

using namespace mm;

static void run_tests() {
    // --- geometry
    const Rect client{100, 50, 1280, 720};
    NormPoint c = normalize(client, 100 + 640, 50 + 360);
    CHECK(c.u > 0.499 && c.u < 0.501);
    CHECK(c.v > 0.499 && c.v < 0.501);
    Point back = denormalize(client, c);
    CHECK_EQ(back.x, 740); CHECK_EQ(back.y, 410);

    // Same fraction lands at the same relative spot in a smaller client.
    const Rect small{0, 0, 640, 360};
    Point s = denormalize(small, normalize(client, 100 + 1279, 50 + 719));
    CHECK_EQ(s.x, 639); CHECK_EQ(s.y, 359);          // clamped inside the client
    Point o = denormalize(small, NormPoint{-1.0, 2.0});
    CHECK_EQ(o.x, 0); CHECK_EQ(o.y, 359);

    const Rect vs{-1920, 0, 3840, 1080};             // two monitors, secondary on the left
    AbsPoint a = to_absolute(vs, -1920, 0);
    CHECK_EQ(a.ax, uint16_t{0}); CHECK_EQ(a.ay, uint16_t{0});
    a = to_absolute(vs, 1919, 1079);
    CHECK_EQ(a.ax, uint16_t{65535}); CHECK_EQ(a.ay, uint16_t{65535});
    a = to_absolute(vs, 0, 540);
    CHECK(a.ax > 32700 && a.ax < 32800);

    // --- grid
    const Rect area{0, 0, 1920, 1080};
    auto g = compute_layout(LayoutSpec{LayoutKind::Grid, 2, 2}, area, 4);
    CHECK_EQ(g.size(), std::size_t{4});
    CHECK((g[0] == Rect{0, 0, 960, 540}));
    CHECK((g[1] == Rect{960, 0, 960, 540}));
    CHECK((g[2] == Rect{0, 540, 960, 540}));
    CHECK((g[3] == Rect{960, 540, 960, 540}));

    // Derived grid: 3 slots -> 2x2 with one empty cell; 5 -> 2 rows x 3 cols.
    int rows, cols;
    derive_grid(3, rows, cols); CHECK_EQ(rows, 2); CHECK_EQ(cols, 2);
    derive_grid(5, rows, cols); CHECK_EQ(rows, 2); CHECK_EQ(cols, 3);
    derive_grid(1, rows, cols); CHECK_EQ(rows, 1); CHECK_EQ(cols, 1);
    auto g5 = compute_layout(LayoutSpec{LayoutKind::Grid, 0, 0}, area, 5);
    CHECK_EQ(g5[4].x, 640); CHECK_EQ(g5[4].y, 540);
    // Cells tile exactly even when the area does not divide evenly.
    auto g3 = compute_layout(LayoutSpec{LayoutKind::Grid, 1, 3}, Rect{0, 0, 1000, 100}, 3);
    CHECK_EQ(g3[0].w + g3[1].w + g3[2].w, 1000);
    CHECK_EQ(g3[2].right(), 1000);

    // --- stacked
    auto st = compute_layout(LayoutSpec{LayoutKind::Stacked}, area, 3);
    CHECK((st[0] == area && st[2] == area));

    // --- pip
    LayoutSpec pip{LayoutKind::Pip, 0, 0, 320, Edge::Right};
    auto p = compute_layout(pip, area, 4);
    CHECK((p[0] == Rect{0, 0, 1600, 1080}));
    CHECK((p[1] == Rect{1600, 0, 320, 360}));
    CHECK((p[3] == Rect{1600, 720, 320, 360}));
    pip.edge = Edge::Bottom;
    p = compute_layout(pip, area, 3);
    CHECK((p[0] == Rect{0, 0, 1920, 760}));
    CHECK((p[1] == Rect{0, 760, 960, 320}));
    CHECK((p[2] == Rect{960, 760, 960, 320}));
    p = compute_layout(pip, area, 1);                 // lone master keeps the strip empty
    CHECK_EQ(p.size(), std::size_t{1});
    CHECK(compute_layout(pip, area, 0).empty());

    // --- assignment
    Assignment as(4);
    CHECK_EQ(as.master(), std::size_t{0});
    as.set_master(2);
    CHECK_EQ(as.master(), std::size_t{2});
    CHECK_EQ(as.slot_of(0), uint32_t{2});             // old master took the vacated slot
    CHECK_EQ(as.slot_of(1), uint32_t{1});
    as.set_master(2);                                 // idempotent
    CHECK_EQ(as.slot_of(0), uint32_t{2});
    as.erase(0);                                      // instances renumber, slots compact
    CHECK_EQ(as.size(), std::size_t{3});
    CHECK_EQ(as.master(), std::size_t{1});            // former instance 2
    CHECK_EQ(as.slot_of(0), uint32_t{1});             // former instance 1 kept slot 1
    CHECK_EQ(as.slot_of(2), uint32_t{2});             // former instance 3 moved from slot 3 to 2
}

TEST_MAIN("layout")
