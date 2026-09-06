#include <thread>
#include <vector>

#include "check.hpp"
#include "mm/input_event.hpp"
#include "mm/spsc_ring.hpp"

using namespace mm;

static InputEvent ev(uint64_t ts) { InputEvent e{}; e.ts = ts; e.kind = EventKind::KeyDown; return e; }

static void run_tests() {
    SpscRing<InputEvent, 8> r;
    CHECK(r.empty());
    for (uint64_t i = 0; i < 8; ++i) CHECK(r.push(ev(i)));
    CHECK(!r.push(ev(99)));           // full
    CHECK_EQ(r.dropped(), std::size_t{1});
    CHECK_EQ(r.size(), std::size_t{8});

    InputEvent out{};
    for (uint64_t i = 0; i < 8; ++i) { CHECK(r.pop(out)); CHECK_EQ(out.ts, i); }
    CHECK(!r.pop(out));
    CHECK(r.empty());

    // Wraparound keeps FIFO order across many laps.
    for (uint64_t i = 0; i < 1000; ++i) { CHECK(r.push(ev(i))); CHECK(r.pop(out)); CHECK_EQ(out.ts, i); }

    // One producer, one consumer, no lost or reordered events.
    SpscRing<InputEvent, 1024> big;
    constexpr uint64_t N = 200000;
    std::thread producer([&] {
        for (uint64_t i = 0; i < N; ++i) while (!big.push(ev(i))) std::this_thread::yield();
    });
    uint64_t expect = 0;
    bool ordered = true;
    while (expect < N) {
        InputEvent e{};
        if (big.pop(e)) { if (e.ts != expect) ordered = false; ++expect; }
        else std::this_thread::yield();
    }
    producer.join();
    CHECK(ordered);
    CHECK_EQ(expect, N);   // every event arrived exactly once (retried pushes count as drops by design)
}

TEST_MAIN("spsc_ring")
