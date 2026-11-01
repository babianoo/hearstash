#pragma once

#include <span>
#include <vector>

// samples, which are from a second, converted to to a second: e.g. a
// capture's 48 kHz to the 16 kHz whisper wants. Lowering the rate first
// filters out what the new rate can't hold, which would otherwise come
// out as noise. The result is as long in time as samples.
// Throws std::runtime_error if the rates are 0 or miniaudio fails.
std::vector<float> resample(std::span<const float> samples, int from, int to);
