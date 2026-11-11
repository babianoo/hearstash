#include "core/media_encoding.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

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

// The value of the bytes at data[at...], lowest first, as WAV has them
static std::uint32_t get(std::span<const std::uint8_t> data, size_t at, int bytes){
    std::uint32_t value = 0;
    for (int i = bytes - 1; i >= 0; --i) value = (value << 8) | data[at + static_cast<size_t>(i)];
    return value;
}

DecodedWav decode_wav(std::span<const std::uint8_t> data){
    const auto is = [&](size_t at, const char* tag){ return std::memcmp(data.data() + at, tag, 4) == 0; };
    if (data.size() < 12 || !is(0, "RIFF") || !is(8, "WAVE")) throw std::runtime_error("It isn't a WAV file");

    DecodedWav out;
    bool have_format = false;
    // Chunks: a 4-byte tag, a 4-byte size, then that many bytes, padded to
    // an even number
    for (size_t at = 12; at + 8 <= data.size();){
        const size_t size = get(data, at + 4, 4);
        const size_t body = at + 8;
        if (size > data.size() - body) throw std::runtime_error("The WAV file is cut off");
        if (is(at, "fmt ")){
            if (size < 16) throw std::runtime_error("The WAV file's format is broken");
            if (get(data, body, 2) != 1 || get(data, body + 2, 2) != 1 || get(data, body + 14, 2) != 16)
                throw std::runtime_error("Only 16-bit mono WAV files can be read");
            out.rate = static_cast<int>(get(data, body + 4, 4));
            if (out.rate <= 0) throw std::runtime_error("The WAV file has no sample rate");
            have_format = true;
        } else if (is(at, "data")){
            if (!have_format) throw std::runtime_error("The WAV file's samples come before its format");
            out.samples.reserve(size / 2);
            for (size_t i = body; i + 2 <= body + size; i += 2){
                const auto value = static_cast<std::int16_t>(get(data, i, 2));
                out.samples.push_back(static_cast<float>(value) / 32767.0f);
            }
            return out;
        }
        at = body + size + (size & 1);
    }
    throw std::runtime_error("The WAV file has no samples");
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
