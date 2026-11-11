#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// samples, which are rate a second, as a WAV file's bytes: 16-bit mono
// PCM, after the 44-byte header. Samples outside -1 to 1 are clipped.
std::vector<std::uint8_t> encode_wav(std::span<const float> samples, int rate);

// What a WAV file holds, read by decode_wav
struct DecodedWav {
    std::vector<float> samples;
    int rate = 0;
};

// The samples in a WAV file's bytes, as encode_wav writes them: 16-bit
// PCM, mono. Chunks other than "fmt " and "data" are skipped. Throws
// std::runtime_error for anything else, e.g. stereo or a cut-off file.
DecodedWav decode_wav(std::span<const std::uint8_t> data);

// data in base64, the way AnkiConnect takes files: e.g. "Man" -> "TWFu"
std::string base64_encode(std::span<const std::uint8_t> data);
