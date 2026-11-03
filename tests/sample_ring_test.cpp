// Tests for SampleRing and GapDetector. No framework: each failed check
// prints where it was, and the exit code is how many failed.
// Run with: ctest --test-dir build

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

#include "core/sample_ring.h"

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)){ \
        std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static void test_empty(){
    SampleRing ring(10);
    CHECK(ring.snapshot(5).empty());
    CHECK(ring.written() == 0);
}

static void test_less_than_asked(){
    SampleRing ring(10);
    const std::vector<float> in{1, 2, 3, 4, 5};
    ring.write(in);
    CHECK(ring.snapshot(3) == std::vector<float>({3, 4, 5}));
    CHECK(ring.snapshot(20) == in);
    CHECK(ring.written() == 5);
}

static void test_wraps(){
    SampleRing ring(4);
    ring.write(std::vector<float>{1, 2, 3});
    ring.write(std::vector<float>{4, 5, 6});
    CHECK(ring.snapshot(4) == std::vector<float>({3, 4, 5, 6}));
    CHECK(ring.snapshot(2) == std::vector<float>({5, 6}));
    CHECK(ring.written() == 6);
}

static void test_more_than_capacity_at_once(){
    SampleRing ring(3);
    ring.write(std::vector<float>{1, 2, 3, 4, 5, 6, 7});
    CHECK(ring.snapshot(3) == std::vector<float>({5, 6, 7}));
    CHECK(ring.written() == 7);
}

static void test_silence(){
    SampleRing ring(6);
    ring.write(std::vector<float>{1, 2});
    ring.write_silence(2);
    ring.write(std::vector<float>{3});
    CHECK(ring.snapshot(5) == std::vector<float>({1, 2, 0, 0, 3}));
    // Far more than fits, like hours of silence
    ring.write_silence(1'000'000);
    CHECK(ring.snapshot(6) == std::vector<float>(6, 0));
    CHECK(ring.written() == 1'000'005);
}

// A writer writes 0, 1, 2... in chunks while snapshots are taken. Every
// snapshot has to be a run of consecutive numbers, none half replaced.
static void test_snapshot_while_writing(){
    // Small numbers, so the floats hold them exactly
    constexpr size_t CAPACITY = 1000, SNAPSHOT = 900, CHUNK = 7;
    constexpr int TOTAL = 3'000'000;
    SampleRing ring(CAPACITY);

    std::atomic<bool> done{false};
    std::thread writer([&]{
        std::vector<float> chunk(CHUNK);
        for (int next = 0; next < TOTAL; next += CHUNK){
            for (size_t i = 0; i < CHUNK; ++i) chunk[i] = static_cast<float>((next + static_cast<int>(i)) % 1'000'000);
            ring.write(chunk);
        }
        done = true;
    });

    int snapshots = 0, bad = 0;
    while (!done){
        const std::vector<float> s = ring.snapshot(SNAPSHOT);
        ++snapshots;
        for (size_t i = 1; i < s.size(); ++i)
            if (s[i] != s[i - 1] + 1 && s[i] != 0) { ++bad; break; }
    }
    writer.join();
    CHECK(bad == 0);
    CHECK(snapshots > 0);
}

static void test_gap_steady(){
    GapDetector gaps(48000);
    std::uint64_t silence = 0;
    // 10 ms chunks, each a little late or early
    for (int i = 1; i <= 1000; ++i){
        const double jitter = (i % 3 - 1) * 0.004;
        silence += gaps.silence_before(i * 0.01 + jitter, 480);
    }
    CHECK(silence == 0);
}

static void test_gap_filled(){
    GapDetector gaps(48000);
    for (int i = 1; i <= 100; ++i) CHECK(gaps.silence_before(i * 0.01, 480) == 0);
    // Nothing for 2 seconds, then a chunk that ends 2.01 s after the last
    const std::uint64_t silence = gaps.silence_before(1.0 + 2.01, 480);
    CHECK(silence > 47900 * 2 && silence < 48100 * 2);
    // Back to steady after it
    CHECK(gaps.silence_before(3.02, 480) == 0);
}

// Chunks held up for a while then arriving all at once mustn't make the
// next real gap shorter
static void test_gap_after_burst(){
    GapDetector gaps(48000);
    for (int i = 1; i <= 100; ++i) gaps.silence_before(i * 0.01, 480);
    // 20 chunks come in a burst at 1.15 s: the first looks like a gap
    // under GAP, the last 5 are ahead of the clock
    for (int i = 0; i < 20; ++i) CHECK(gaps.silence_before(1.15, 480) == 0);
    // Then 1 second of nothing
    const std::uint64_t silence = gaps.silence_before(2.16, 480);
    CHECK(silence > 47500 && silence < 48500);
}

// Started before the first chunk: the wait for it is a gap too
static void test_gap_before_first(){
    GapDetector gaps(48000);
    gaps.start(10.0);
    // Started again once running: ignored
    gaps.start(11.0);
    const std::uint64_t silence = gaps.silence_before(13.0, 480);
    CHECK(silence > 47900 * 3 - 480 && silence < 48100 * 3 - 480);
    CHECK(gaps.silence_before(13.01, 480) == 0);

    // Not started: the first chunk starts the clock
    GapDetector unstarted(48000);
    CHECK(unstarted.silence_before(13.0, 480) == 0);
}

int main(){
    test_empty();
    test_less_than_asked();
    test_wraps();
    test_more_than_capacity_at_once();
    test_silence();
    test_snapshot_while_writing();
    test_gap_steady();
    test_gap_filled();
    test_gap_after_burst();
    test_gap_before_first();
    if (failures == 0) std::puts("All sample ring tests passed");
    return failures;
}
