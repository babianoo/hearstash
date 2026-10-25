#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// SHA-1, to check a download against the hash its source publishes.
// Written out here rather than taken from a library: the app has none
// that offers it on every platform, and it's short.
//
// Not for anything security-related: SHA-1 collisions can be made on
// purpose. It catches a damaged or truncated download, which is all it's
// used for here.
//
// Feed it with update() in pieces of any size, then hex_digest().
class Sha1 {
public:
    void update(const void* data, std::size_t size);
    // e.g. "a9993e364706816aba3e25717850c26c9cd0d89d" for "abc". Finishes
    // the hash, so call it once, after the last update().
    std::string hex_digest();

private:
    std::array<std::uint32_t, 5> state{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    // SHA-1 works on 64-byte blocks; this holds one until it's full
    std::array<unsigned char, 64> block{};
    std::size_t block_size = 0;
    // Bytes fed so far, which the padding ends with
    std::uint64_t length = 0;

    void process_block();
};
