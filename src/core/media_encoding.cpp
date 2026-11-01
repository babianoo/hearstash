#include "core/media_encoding.h"

#include <algorithm>
#include <cmath>

// Appends value's lowest bytes, lowest first, as WAV wants
static void put(std::vector<std::uint8_t>& out, std::uint32_t value, int bytes){
    for (int i = 0; i < bytes; ++i) out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

std::vector<std::uint8_t> encode_wav(std::span<const float> samples, int rate){
    const std::uint32_t data_size = static_cast<std::uint32_t>(samples.size() * 2);
    std::vector<std::uint8_t> out;
    out.reserve(44 + data_size);

    const auto tag = [&](const char* name){ out.insert(out.end(), name, name + 4); };
    tag("RIFF");
    put(out, 36 + data_size, 4);    // What follows
    tag("WAVE");
    tag("fmt ");
    put(out, 16, 4);                // Size of this chunk
    put(out, 1, 2);                 // PCM
    put(out, 1, 2);                 // Mono
    put(out, static_cast<std::uint32_t>(rate), 4);
    put(out, static_cast<std::uint32_t>(rate) * 2, 4);    // Bytes a second
    put(out, 2, 2);                 // Bytes a sample
    put(out, 16, 2);                // Bits a sample
    tag("data");
    put(out, data_size, 4);

    for (float sample : samples){
        const auto value = static_cast<std::int16_t>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
        put(out, static_cast<std::uint16_t>(value), 2);
    }
    return out;
}

std::string base64_encode(std::span<const std::uint8_t> data){
    static constexpr char DIGITS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3){
        // Three bytes make four 6-bit digits; a last group of one or two
        // is padded with '='
        const size_t n = std::min<size_t>(3, data.size() - i);
        std::uint32_t group = static_cast<std::uint32_t>(data[i]) << 16;
        if (n > 1) group |= static_cast<std::uint32_t>(data[i + 1]) << 8;
        if (n > 2) group |= data[i + 2];
        out += DIGITS[(group >> 18) & 63];
        out += DIGITS[(group >> 12) & 63];
        out += n > 1 ? DIGITS[(group >> 6) & 63] : '=';
        out += n > 2 ? DIGITS[group & 63] : '=';
    }
    return out;
}
