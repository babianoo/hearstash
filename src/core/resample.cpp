#include "core/resample.h"

#include <algorithm>
#include <format>
#include <stdexcept>

// miniaudio, with the MA_NO_* definitions CMake gives it
#include <miniaudio.h>

std::vector<float> resample(std::span<const float> samples, int from, int to){
    if (from <= 0 || to <= 0) throw std::runtime_error(std::format("Can't resample from {} Hz to {} Hz", from, to));
    if (from == to) return {samples.begin(), samples.end()};

    // Linear, with miniaudio's low-pass filter: plenty for speech going to
    // whisper
    const ma_resampler_config config = ma_resampler_config_init(
        ma_format_f32, 1, static_cast<ma_uint32>(from), static_cast<ma_uint32>(to), ma_resample_algorithm_linear);
    ma_resampler resampler;
    ma_result result = ma_resampler_init(&config, nullptr, &resampler);
    if (result != MA_SUCCESS) throw std::runtime_error(std::format("Couldn't resample: {}", ma_result_description(result)));

    // The filter holds back the last few samples, so silence after the end
    // pushes them out. Its delay also puts silence before the start, which
    // is cut off below.
    std::vector<float> padded(samples.begin(), samples.end());
    padded.resize(samples.size() + ma_resampler_get_input_latency(&resampler));
    const auto delay = static_cast<size_t>(ma_resampler_get_output_latency(&resampler));

    ma_uint64 out_count = 0;
    ma_resampler_get_expected_output_frame_count(&resampler, padded.size(), &out_count);
    std::vector<float> out(static_cast<size_t>(out_count));
    ma_uint64 in_count = padded.size();
    result = ma_resampler_process_pcm_frames(&resampler, padded.data(), &in_count, out.data(), &out_count);
    ma_resampler_uninit(&resampler, nullptr);
    if (result != MA_SUCCESS) throw std::runtime_error(std::format("Couldn't resample: {}", ma_result_description(result)));
    out.resize(static_cast<size_t>(out_count));

    const size_t wanted = static_cast<size_t>(static_cast<double>(samples.size()) * to / from);
    const size_t begin = std::min(delay, out.size());
    const size_t end = std::min(begin + wanted, out.size());
    return {out.begin() + static_cast<std::ptrdiff_t>(begin), out.begin() + static_cast<std::ptrdiff_t>(end)};
}
