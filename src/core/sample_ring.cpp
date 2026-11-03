#include "core/sample_ring.h"

#include <algorithm>

SampleRing::SampleRing(size_t capacity)
    : size(capacity), samples(std::make_unique<std::atomic<float>[]>(capacity)){}

void SampleRing::write(std::span<const float> from){
    put(from.data(), from.size());
}

void SampleRing::write_silence(size_t count){
    put(nullptr, count);
}

void SampleRing::put(const float* from, size_t count){
    // Only the writer changes them, so its own reads can be relaxed
    const std::uint64_t at = written_.load(std::memory_order_relaxed);
    // Only the last size samples would be left anyway, e.g. of hours of
    // silence
    const size_t skip = count > size ? count - size : 0;

    // Announced before the samples change: a snapshot that sees one of
    // them changed is then sure to see this too
    started.store(at + count, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    for (size_t i = skip; i < count; ++i)
        samples[(at + i) % size].store(from ? from[i] : 0.0f, std::memory_order_relaxed);
    // Release: a snapshot that sees the new count sees the samples too
    written_.store(at + count, std::memory_order_release);
}

std::vector<float> SampleRing::snapshot(size_t count) const {
    std::vector<float> out;
    for (;;){
        const std::uint64_t end = written_.load(std::memory_order_acquire);
        const std::uint64_t n = std::min<std::uint64_t>({count, size, end});
        const std::uint64_t begin = end - n;

        out.resize(n);
        for (std::uint64_t i = 0; i < n; ++i)
            out[i] = samples[(begin + i) % size].load(std::memory_order_relaxed);

        // Pairs with the writer's fence. The sample at begin, the oldest
        // copied, is only replaced by the one size later, so if the writer
        // hasn't started on that one, everything copied is as it was.
        std::atomic_thread_fence(std::memory_order_acquire);
        if (started.load(std::memory_order_relaxed) <= begin + size) return out;
    }
}

void GapDetector::start(double now){
    if (started) return;
    started = true;
    origin = now;
    counted = 0;
}

std::uint64_t GapDetector::silence_before(double now, size_t count){
    if (!started){
        // The first chunk ends now
        started = true;
        origin = now - static_cast<double>(count) / rate;
        counted = count;
        return 0;
    }

    // How many samples there should be by the end of this chunk
    const double expected = (now - origin) * rate;
    const double missing = expected - static_cast<double>(counted + count);
    const std::uint64_t silence = missing > GAP * rate ? static_cast<std::uint64_t>(missing) : 0;
    counted += silence + count;

    // More than the time allows, e.g. a burst of chunks that were held up.
    // The origin moves so it's never ahead: otherwise the surplus would be
    // taken off the next real gap, and over hours the audio clock running
    // a little fast would add up to seconds.
    if (static_cast<double>(counted) > expected) origin = now - static_cast<double>(counted) / rate;
    return silence;
}
