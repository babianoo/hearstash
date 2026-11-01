#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// samples, which are rate a second, as a WAV file's bytes: 16-bit mono
// PCM, after the 44-byte header. Samples outside -1 to 1 are clipped.
std::vector<std::uint8_t> encode_wav(std::span<const float> samples, int rate);

// data in base64, the way AnkiConnect takes files: e.g. "Man" -> "TWFu"
std::string base64_encode(std::span<const std::uint8_t> data);
