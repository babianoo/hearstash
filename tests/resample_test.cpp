// Tests for resample(). No framework, like sample_ring_test.cpp.

#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include "core/resample.h"

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)){ \
        std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static std::vector<float> sine(double hz, int rate, double seconds){
    std::vector<float> out(static_cast<size_t>(rate * seconds));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<float>(0.5 * std::sin(2 * std::numbers::pi * hz * static_cast<double>(i) / rate));
    return out;
}

// Root mean square of the middle half, away from the filter's edges
static double rms(const std::vector<float>& samples){
    double sum = 0;
    const size_t from = samples.size() / 4, to = samples.size() * 3 / 4;
    for (size_t i = from; i < to; ++i) sum += samples[i] * samples[i];
    return std::sqrt(sum / static_cast<double>(to - from));
}

int main(){
    // Same length in time
    const std::vector<float> speech = resample(sine(440, 48000, 2), 48000, 16000);
    CHECK(speech.size() == 32000);
    // What 16 kHz can hold comes through at about the same level...
    CHECK(std::abs(rms(speech) - 0.5 / std::sqrt(2)) < 0.03);
    // ...and what it can't (over 8 kHz) is filtered out, not folded down
    const std::vector<float> high = resample(sine(12000, 48000, 2), 48000, 16000);
    CHECK(rms(high) < 0.1);

    CHECK(resample(std::vector<float>{1, 2, 3}, 16000, 16000) == std::vector<float>({1, 2, 3}));
    CHECK(resample({}, 48000, 16000).empty());

    if (failures == 0) std::puts("All resample tests passed");
    return failures;
}
