// Tests for deinflect(). No framework, like sample_ring_test.cpp.

#include <algorithm>
#include <cstdio>
#include <string>

#include "core/deinflect.h"

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)){ \
        std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

// Whether inflected could be word, a word of that kind
static bool gives(const char* inflected, const char* word, unsigned kind, bool expected = true){
    const auto found = deinflect(inflected);
    const bool is = std::ranges::any_of(found, [&](const Deinflection& d){ return d.word == word && (d.kinds & kind); });
    if (is != expected) std::fprintf(stderr, "  %s %s %s\n", inflected, is ? "gives" : "doesn't give", word);
    return is;
}

static void test_ichidan(){
    for (const char* form : {"食べます", "食べました", "食べません", "食べない", "食べなかった", "食べて", "食べた",
                             "食べたら", "食べたり", "食べられる", "食べられない", "食べさせる", "食べさせられた",
                             "食べよう", "食べれば", "食べろ", "食べたい", "食べたくない", "食べたくなかった",
                             "食べている", "食べていた", "食べてる", "食べてた", "食べちゃった", "食べてしまいました",
                             "食べすぎた", "食べながら", "食べず", "食べなきゃ", "食べておく", "食べとく", "食べ",
                             "食べてください", "食べやすい", "食べれる", "食べてみたい"})
        CHECK(gives(form, "食べる", KIND_V1));
}

static void test_godan(){
    // Every ending's て and た form
    CHECK(gives("買って", "買う", KIND_V5));
    CHECK(gives("書いた", "書く", KIND_V5));
    CHECK(gives("泳いで", "泳ぐ", KIND_V5));
    CHECK(gives("話した", "話す", KIND_V5));
    CHECK(gives("待って", "待つ", KIND_V5));
    CHECK(gives("死んだ", "死ぬ", KIND_V5));
    CHECK(gives("遊んで", "遊ぶ", KIND_V5));
    CHECK(gives("読んだ", "読む", KIND_V5));
    CHECK(gives("取って", "取る", KIND_V5));
    CHECK(gives("行って", "行く", KIND_V5));
    CHECK(gives("行った", "行く", KIND_V5));

    for (const char* form : {"悩んでた", "悩んでいる", "悩みます", "悩まない", "悩まなかった", "悩まれる", "悩ませる",
                             "悩まされる", "悩める", "悩もう", "悩めば", "悩め", "悩みたい", "悩んじゃう", "悩んどく",
                             "悩んでしまった", "悩まず", "悩みすぎる", "悩み", "悩んだら", "悩むな"})
        CHECK(gives(form, "悩む", KIND_V5));
    CHECK(gives("書かなきゃ", "書く", KIND_V5));
    CHECK(gives("言わせてもらう", "言う", KIND_V5));
    CHECK(gives("おっしゃいました", "おっしゃる", KIND_V5));
}

static void test_irregular(){
    for (const char* form : {"した", "して", "しない", "します", "しました", "される", "させる", "しよう", "すれば",
                             "しろ", "している", "してた", "しなかった", "したい", "せず"})
        CHECK(gives((std::string("勉強") + form).c_str(), "勉強する", KIND_VS));
    CHECK(gives("勉強できる", "勉強する", KIND_VS));
    // The irregular words on their own
    CHECK(gives("した", "する", KIND_VS));
    CHECK(gives("しなかった", "する", KIND_VS));
    // But an ending on its own is no word
    CHECK(!gives("た", "る", KIND_V1, false));
    CHECK(!gives("かった", "い", KIND_ADJ_I, false));

    for (const char* form : {"来た", "来て", "来ない", "来ます", "来られる", "来よう", "来れば", "来い", "来ている"})
        CHECK(gives(form, "来る", KIND_VK));
    CHECK(gives("きた", "くる", KIND_VK));
    CHECK(gives("こない", "くる", KIND_VK));
    CHECK(gives("こられる", "くる", KIND_VK));
    CHECK(gives("持ってきた", "持ってくる", KIND_VK));

    CHECK(gives("論じた", "論ずる", KIND_VZ));
}

static void test_adjectives(){
    for (const char* form : {"高く", "高くて", "高くない", "高かった", "高くなかった", "高ければ", "高さ", "高そう",
                             "高すぎる", "高かったら", "高くなる", "高くなった"})
        CHECK(gives(form, "高い", KIND_ADJ_I));
    CHECK(gives("よかった", "いい", KIND_ADJ_I));
    CHECK(gives("よくない", "いい", KIND_ADJ_I));
}

static void test_shape(){
    // The text itself is first, as any word
    const auto found = deinflect("食べなかった");
    CHECK(!found.empty() && found.front().word == "食べなかった" && found.front().kinds == 0 && found.front().steps.empty());
    // What was taken off, outermost first
    const auto taberu = std::ranges::find_if(found, [](const Deinflection& d){ return d.word == "食べる" && (d.kinds & KIND_V1); });
    CHECK(taberu != found.end() && taberu->steps == std::vector<std::string>({"past", "negative"}));

    // A past isn't inflected again: 食べたた is nothing
    CHECK(!gives("食べたた", "食べる", KIND_V1, false));
    // Not an adjective's ending on a verb's kind: 食べる isn't 食べい
    const auto takai = deinflect("高かった");
    CHECK(std::ranges::none_of(takai, [](const Deinflection& d){ return d.word == "高い" && d.kinds == KIND_V1; }));

    // Nothing left of a word that's only an ending
    CHECK(deinflect("た").size() >= 1 && deinflect("た").front().word == "た");
    CHECK(deinflect("").size() == 1);
    // Bounded, however long the text
    std::string longer;
    for (int i = 0; i < 200; ++i) longer += "たくなかった";
    CHECK(deinflect(longer).size() <= 400);

    CHECK(word_kinds("v5") == KIND_V5);
    CHECK(word_kinds("vs v5") == (KIND_VS | KIND_V5));
    CHECK(word_kinds("adj-i") == KIND_ADJ_I);
    CHECK(word_kinds("") == 0);
    CHECK(word_kinds("n") == 0);
}

int main(){
    test_ichidan();
    test_godan();
    test_irregular();
    test_adjectives();
    test_shape();
    if (failures == 0) std::puts("All deinflection tests passed");
    return failures;
}
