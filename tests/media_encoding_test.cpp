// Tests for core/media_encoding.h. No framework, like sample_ring_test.cpp.

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#include "core/media_encoding.h"

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)){ \
        std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static std::string base64(const std::string& text){
    return base64_encode({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

static std::uint32_t read(const std::vector<std::uint8_t>& bytes, size_t at, int size){
    std::uint32_t value = 0;
    for (int i = size - 1; i >= 0; --i) value = (value << 8) | bytes[at + static_cast<size_t>(i)];
    return value;
}

int main(){
    // RFC 4648's examples
    CHECK(base64("") == "");
    CHECK(base64("f") == "Zg==");
    CHECK(base64("fo") == "Zm8=");
    CHECK(base64("foo") == "Zm9v");
    CHECK(base64("foob") == "Zm9vYg==");
    CHECK(base64("fooba") == "Zm9vYmE=");
    CHECK(base64("foobar") == "Zm9vYmFy");
    const std::vector<std::uint8_t> high{0xFF, 0xFE, 0xFD};
    CHECK(base64_encode(high) == "//79");

    const std::vector<float> samples{0.0f, 1.0f, -1.0f, 2.0f, 0.5f};
    const std::vector<std::uint8_t> wav = encode_wav(samples, 48000);
    CHECK(wav.size() == 44 + samples.size() * 2);
    CHECK(std::memcmp(wav.data(), "RIFF", 4) == 0);
    CHECK(read(wav, 4, 4) == wav.size() - 8);
    CHECK(std::memcmp(wav.data() + 8, "WAVEfmt ", 8) == 0);
    CHECK(read(wav, 22, 2) == 1);          // Mono
    CHECK(read(wav, 24, 4) == 48000);
    CHECK(read(wav, 34, 2) == 16);
    CHECK(std::memcmp(wav.data() + 36, "data", 4) == 0);
    CHECK(read(wav, 40, 4) == samples.size() * 2);
    CHECK(read(wav, 44, 2) == 0);
    CHECK(read(wav, 46, 2) == 32767);
    CHECK(read(wav, 48, 2) == 0x8001);     // -32767
    CHECK(read(wav, 50, 2) == 32767);      // 2 is clipped to 1
    CHECK(read(wav, 52, 2) == 16384);

    // Read back as written, clipped and rounded to 16 bits
    const DecodedWav decoded = decode_wav(wav);
    CHECK(decoded.rate == 48000);
    CHECK(decoded.samples.size() == samples.size());
    if (decoded.samples.size() == samples.size()){
        CHECK(decoded.samples[0] == 0.0f);
        CHECK(decoded.samples[1] == 1.0f);
        CHECK(decoded.samples[2] == -1.0f);
        CHECK(decoded.samples[3] == 1.0f);
        CHECK(decoded.samples[4] > 0.4999f && decoded.samples[4] < 0.5001f);
    }
    // Not a WAV file, or cut off
    const auto throws = [](std::vector<std::uint8_t> bytes){
        try {
            decode_wav(bytes);
        } catch (const std::exception&){
            return true;
        }
        return false;
    };
    CHECK(throws({}));
    CHECK(throws({'R', 'I', 'F', 'F', 0, 0, 0, 0, 'A', 'V', 'I', ' '}));
    std::vector<std::uint8_t> cut(wav.begin(), wav.end() - 4);
    CHECK(throws(cut));
    // Stereo isn't what encode_wav writes
    std::vector<std::uint8_t> stereo = wav;
    stereo[22] = 2;
    CHECK(throws(stereo));

    if (failures == 0) std::puts("All media encoding tests passed");
    return failures;
}
