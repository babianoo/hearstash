#include "core/lookup_text.h"

#include <cstdint>

// The code point that starts at text[at], and how many bytes it takes.
// A byte that doesn't start valid UTF-8 counts as a code point of its own.
static char32_t decode(std::string_view text, size_t at, size_t& length){
    const auto byte = [&](size_t i){ return static_cast<std::uint8_t>(text[at + i]); };
    const std::uint8_t first = byte(0);
    length = first < 0x80 ? 1 : (first >> 5) == 0x6 ? 2 : (first >> 4) == 0xE ? 3 : (first >> 3) == 0x1E ? 4 : 0;
    if (length == 0 || at + length > text.size()){
        length = 1;
        return first;
    }
    char32_t c = length == 1 ? first : first & (0x7F >> length);
    for (size_t i = 1; i < length; ++i){
        if ((byte(i) >> 6) != 0x2){
            length = 1;
            return first;
        }
        c = (c << 6) | (byte(i) & 0x3F);
    }
    return c;
}

// Spaces and punctuation, Japanese and Western, and symbols like ♪
static bool is_filler(char32_t c){
    if (c < 0x80) return !((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
    // 々, 〆 and 〇 are in the CJK punctuation block, but are parts of words
    if (c >= 0x3005 && c <= 0x3007) return false;
    return (c >= 0x3000 && c <= 0x303F)     // 、。「」『』〜 and the wide space
        || c == 0x30FB                      // ・
        || (c >= 0x2000 && c <= 0x206F)     // …‥ “” — and other spaces
        || (c >= 0x2600 && c <= 0x26FF)     // ♪ and other symbols
        || (c >= 0xFF01 && c <= 0xFF0F)     // ！＂＃ ... ／
        || (c >= 0xFF1A && c <= 0xFF20)     // ：；＜ ... ＠
        || (c >= 0xFF3B && c <= 0xFF40)     // ［＼］ ... ｀
        || (c >= 0xFF5B && c <= 0xFF65)     // ｛｜｝～ and half-width 。「」、・
        || c == 0x00A0;                     // No-break space
}

std::string lookup_text(std::string_view selection){
    // The first byte past the leading filler, and the end of the last code
    // point that isn't filler
    size_t begin = selection.size(), end = 0;
    for (size_t at = 0, length = 0; at < selection.size(); at += length){
        if (is_filler(decode(selection, at, length))) continue;
        if (begin == selection.size()) begin = at;
        end = at + length;
    }
    if (begin >= end) return "";
    return std::string(selection.substr(begin, end - begin));
}
