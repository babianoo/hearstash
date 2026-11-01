// Tests for guess_fields(). No framework, like sample_ring_test.cpp.

#include <cstdio>

#include "core/anki_fields.h"

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)){ \
        std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static std::string guess(const std::vector<std::string>& fields, CardPart part){
    return guess_fields(fields)[static_cast<size_t>(part)];
}

int main(){
    // Anki's own Basic
    const std::vector<std::string> basic{"Front", "Back"};
    CHECK(guess(basic, CardPart::Word) == "Front");
    CHECK(guess(basic, CardPart::Meaning) == "Back");
    CHECK(guess(basic, CardPart::Audio).empty());
    CHECK(guess(basic, CardPart::Picture).empty());

    // Like the note types made for mining
    const std::vector<std::string> mining{"Expression", "ExpressionFurigana", "ExpressionReading",
                                          "ExpressionAudio", "MainDefinition", "Sentence",
                                          "SentenceFurigana", "SentenceAudio", "Picture"};
    CHECK(guess(mining, CardPart::Word) == "Expression");
    CHECK(guess(mining, CardPart::Reading) == "ExpressionReading");
    CHECK(guess(mining, CardPart::Meaning) == "MainDefinition");
    CHECK(guess(mining, CardPart::Sentence) == "Sentence");
    CHECK(guess(mining, CardPart::Audio) == "SentenceAudio");
    CHECK(guess(mining, CardPart::Picture) == "Picture");

    // Spaces and case don't matter
    const std::vector<std::string> spaced{"vocab kanji", "Vocab Reading", "Vocab English", "sentence audio", "Image"};
    CHECK(guess(spaced, CardPart::Word) == "vocab kanji");
    CHECK(guess(spaced, CardPart::Reading) == "Vocab Reading");
    CHECK(guess(spaced, CardPart::Meaning) == "Vocab English");
    CHECK(guess(spaced, CardPart::Audio) == "sentence audio");
    CHECK(guess(spaced, CardPart::Picture) == "Image");
    CHECK(guess(spaced, CardPart::Sentence).empty());

    CHECK(guess({}, CardPart::Word).empty());

    if (failures == 0) std::puts("All anki_fields tests passed");
    return failures;
}
