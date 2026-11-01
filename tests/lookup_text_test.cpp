// Tests for lookup_text(). No framework, like sample_ring_test.cpp.

#include <cstdio>

#include "core/lookup_text.h"

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)){ \
        std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

int main(){
    CHECK(lookup_text("天気") == "天気");
    CHECK(lookup_text("  天気 \n") == "天気");
    CHECK(lookup_text("「天気」、") == "天気");
    CHECK(lookup_text("　いい天気ですね。") == "いい天気ですね");
    // Punctuation inside is kept
    CHECK(lookup_text("今日は、いい") == "今日は、いい");
    // Parts of words that are in the punctuation block
    CHECK(lookup_text("人々") == "人々");
    CHECK(lookup_text("〇") == "〇");
    // The long vowel mark is a part of a word too
    CHECK(lookup_text("ラーメン！") == "ラーメン");
    CHECK(lookup_text("eat.") == "eat");

    // Nothing worth looking up
    CHECK(lookup_text("").empty());
    CHECK(lookup_text("。").empty());
    CHECK(lookup_text("、 。…").empty());
    CHECK(lookup_text("♪").empty());
    CHECK(lookup_text("！？").empty());
    CHECK(lookup_text("・").empty());

    // Broken UTF-8 isn't dropped as filler
    CHECK(lookup_text("\xE5\x85") == "\xE5\x85");

    if (failures == 0) std::puts("All lookup_text tests passed");
    return failures;
}
