#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

// A fixed amount of audio that's written in a circle: once it's full, each
// new sample replaces the oldest. For AudioBuffer, kept apart from the
// audio device so it can be tested without one.
//
// One thread writes, and any thread can take a snapshot meanwhile, without
// a lock: the writer never waits or allocates, which the audio thread
// mustn't. A snapshot shorter than the ring by some margin can't be
// overwritten while it's being copied, as long as the writer writes less
// than the margin meanwhile.
class SampleRing {
public:
    // Allocates capacity samples, all silence
    explicit SampleRing(size_t capacity);

    // The writer's thread only
    void write(std::span<const float> samples);
    void write_silence(size_t count);

    // The newest count samples, oldest first, or as many as were written if
    // that's fewer. Any thread. If the writer laps it while it copies, it
    // starts again on the samples that are newest then.
    std::vector<float> snapshot(size_t count) const;

    // How many samples were ever written, silence included. Only goes up.
    std::uint64_t written() const { return written_.load(std::memory_order_acquire); }
    size_t capacity() const { return size; }

private:
    size_t size;
    // Atomic so the snapshot's reads and the writer's writes of the same
    // sample, when it laps the snapshot, aren't a data race. Relaxed, they
    // compile to plain loads and stores.
    std::unique_ptr<std::atomic<float>[]> samples;
    // How far the writer has started writing, then how far it's done. A
    // snapshot reads written_ before copying and started after, which tells
    // it whether anything it copied may have been replaced meanwhile.
    std::atomic<std::uint64_t> started{0};
    std::atomic<std::uint64_t> written_{0};

    // Writes count samples from from, or silence if from is null
    void put(const float* from, size_t count);
};

// Tells how much silence to write for a gap in the audio. Some systems
// (WASAPI's loopback) deliver nothing at all while nothing plays, so
// without this "the last 30 seconds" would be the last 30 seconds of
// sound, not of time.
//
// It compares how much audio arrived with how much time went by. Gaps
// shorter than GAP are ignored: audio arrives in uneven chunks, a little
// late or early. A gap is only reported at the chunk after it, which
// still holds the audio that came after the gap.
class GapDetector {
public:
    // A gap shorter than this, in seconds, isn't filled
    static constexpr double GAP = 0.2;

    explicit GapDetector(int sample_rate) : rate(sample_rate) {}

    // Counts time from now, before any chunk has arrived, so that waiting
    // for the first sound is silence too. Without it, time is counted from
    // the first chunk. Does nothing once it has started.
    void start(double now);

    // Called with each chunk as it arrives, before writing it: now is the
    // time in seconds, from any fixed starting point, and count the chunk's
    // samples. Returns how many samples of silence to write before it.
    // Doesn't allocate or lock, so it can run on the audio thread.
    std::uint64_t silence_before(double now, size_t count);

private:
    int rate;
    bool started = false;
    // The time that sample 0 would have been at, if every sample since had
    // come exactly on time
    double origin = 0;
    // Samples counted so far, silence included
    std::uint64_t counted = 0;
};
