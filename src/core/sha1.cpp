#include "core/sha1.h"

#include <algorithm>
#include <bit>
#include <format>

// Following FIPS 180-4, section 6.1

void Sha1::update(const void* data, std::size_t size){
    const auto* bytes = static_cast<const unsigned char*>(data);
    length += size;
    while (size > 0){
        const std::size_t n = std::min(size, block.size() - block_size);
        std::copy_n(bytes, n, block.data() + block_size);
        block_size += n;
        bytes += n;
        size -= n;
        if (block_size == block.size()) process_block();
    }
}

std::string Sha1::hex_digest(){
    const std::uint64_t bits = length * 8;

    // A 1 bit, then 0 bits until 8 bytes short of a block, then the
    // length in bits. If the length doesn't fit, the zeros fill this
    // block and the next one gets it.
    block[block_size++] = 0x80;
    if (block_size > 56){
        std::fill(block.begin() + static_cast<std::ptrdiff_t>(block_size), block.end(), 0);
        process_block();
    }
    std::fill(block.begin() + static_cast<std::ptrdiff_t>(block_size), block.begin() + 56, 0);
    for (int i = 0; i < 8; ++i) block[56 + i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
    process_block();

    std::string hex;
    for (std::uint32_t word : state) hex += std::format("{:08x}", word);
    return hex;
}

void Sha1::process_block(){
    // The block as 16 big-endian words, extended to 80
    std::array<std::uint32_t, 80> w;
    for (int t = 0; t < 16; ++t){
        w[t] = std::uint32_t{block[4 * t]} << 24 | std::uint32_t{block[4 * t + 1]} << 16
             | std::uint32_t{block[4 * t + 2]} << 8 | std::uint32_t{block[4 * t + 3]};
    }
    for (int t = 16; t < 80; ++t) w[t] = std::rotl(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);

    auto [a, b, c, d, e] = state;
    for (int t = 0; t < 80; ++t){
        std::uint32_t f, k;
        if (t < 20){
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (t < 40){
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (t < 60){
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        const std::uint32_t temp = std::rotl(a, 5) + f + e + k + w[t];
        e = d;
        d = c;
        c = std::rotl(b, 30);
        b = a;
        a = temp;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;

    block_size = 0;
}
