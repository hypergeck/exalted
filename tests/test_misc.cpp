#include "check.hpp"
#include "mm/args.hpp"
#include "mm/key_names.hpp"
#include "mm/latency_stats.hpp"

using namespace mm;

static void run_tests() {
    // --- latency stats
    LatencyStats s;
    CHECK_EQ(s.percentile_us(99), uint32_t{0});
    for (uint32_t i = 1; i <= 100; ++i) s.record(i * 100);   // 100us .. 10ms
    CHECK_EQ(s.count(), uint64_t{100});
    CHECK_EQ(s.min_us(), uint32_t{100});
    CHECK_EQ(s.max_us(), uint32_t{10000});
    CHECK_EQ(s.mean_us(), uint32_t{5050});
    CHECK(s.percentile_us(50) >= 5000 && s.percentile_us(50) <= 5025);
    CHECK(s.percentile_us(99) >= 9900 && s.percentile_us(99) <= 9925);
    s.record(10'000'000);                                     // overflow lands in the last bucket
    CHECK_EQ(s.percentile_us(100), uint32_t{LatencyStats::kBuckets * LatencyStats::kBucketUs});
    s.reset();
    CHECK_EQ(s.count(), uint64_t{0});

    // --- key names
    CHECK_EQ(*parse_key("w"), uint16_t{'W'});
    CHECK_EQ(*parse_key("7"), uint16_t{'7'});
    CHECK_EQ(*parse_key("F12"), uint16_t{0x7B});
    CHECK_EQ(*parse_key("f1"), uint16_t{0x70});
    CHECK_EQ(*parse_key("CapsLock"), uint16_t{0x14});
    CHECK_EQ(*parse_key("0x20"), uint16_t{0x20});
    CHECK_EQ(*parse_key("space"), uint16_t{0x20});
    CHECK(!parse_key("F25"));
    CHECK(!parse_key("0x1FF"));
    CHECK(!parse_key("NOPE"));
    CHECK(!parse_key(""));
    CHECK(key_name(0x7B) == "F12");
    CHECK(key_name('Q') == "Q");
    CHECK(key_name(0x14) == "CAPSLOCK");
    CHECK(key_name(0xF0) == "0xF0");

    // --- args
    const char* argv[] = {"prog", "run", "--adopt", "123", "--adopt=456", "--borderless", "--keys", "W,A, S,,D", "--n", "0x10", "extra"};
    Args a(static_cast<int>(sizeof(argv) / sizeof(argv[0])), const_cast<char**>(argv));
    CHECK_EQ(a.positional().size(), std::size_t{2});
    CHECK(a.positional()[0] == "run");
    CHECK(a.positional()[1] == "extra");
    CHECK_EQ(a.all("adopt").size(), std::size_t{2});
    CHECK(a.all("adopt")[1] == "456");
    CHECK(a.has("borderless"));
    CHECK(a.get("borderless")->empty());
    CHECK(!a.has("missing"));
    CHECK_EQ(a.get_int("n", 0), 16L);
    CHECK_EQ(a.get_int("missing", 7), 7L);
    auto keys = split_list(*a.get("keys"));
    CHECK_EQ(keys.size(), std::size_t{4});
    CHECK(keys[2] == "S");
}

TEST_MAIN("misc")
