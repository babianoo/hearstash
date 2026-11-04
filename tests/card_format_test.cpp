// Tests for format_card(). No framework, like sample_ring_test.cpp.

#include <cstdio>
#include <string>

#include "core/card_format.h"

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)){ \
        std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static std::string field(const std::vector<std::pair<std::string, std::string>>& fields, const std::string& name){
    for (const auto& [n, html] : fields) if (n == name) return html;
    return "(missing)";
}

static const CardContent CARD{
    .word = "悩む", .reading = "なやむ", .meaning = "1. to be worried\n2. to suffer (from)",
    .sentence = "私だってもうちょっと悩んでたかも", .highlight = "悩んでた",
    .audio_file = "hearstash_1.wav", .picture_file = "hearstash_1.jpg",
};

// Like the Immersion Deck: the sentence on the front, everything about
// the word on the back, then the audio, and the picture on its own
static void test_immersion_layout(){
    AnkiSettings s;
    s.sentence_field = "Front";
    s.word_field = s.reading_field = s.meaning_field = s.audio_field = "Back";
    s.picture_field = "Picture";
    const auto fields = format_card(CARD, s);
    CHECK(fields.size() == 3);
    CHECK(field(fields, "Front") ==
          "私だってもうちょっと<span style=\"font-size:1.3em; color: #00828a;\">悩んでた</span>かも");
    CHECK(field(fields, "Back") ==
          "悩む【なやむ】 <br><div style=\"text-align: left;\" class=\"yomitan-glossary\"><ol>"
          "<li data-dictionary=\"Jisho.org\"><span><ol>"
          "<li style=\"list-style-type: &quot;①&quot;;\">to be worried</li>"
          "<li style=\"list-style-type: &quot;②&quot;;\">to suffer (from)</li>"
          "</ol></span></li></ol></div><br><br><a href=\"\"></a><br>[sound:hearstash_1.wav]");
    CHECK(field(fields, "Picture") == "<img alt=\"snapshot\" src=\"hearstash_1.jpg\">");
}

// A field each, like note types made for mining
static void test_a_field_each(){
    AnkiSettings s;
    s.word_field = "Expression";
    s.reading_field = "Reading";
    s.meaning_field = "Meaning";
    s.sentence_field = "Sentence";
    s.audio_field = "SentenceAudio";
    s.picture_field = "";
    const auto fields = format_card(CARD, s);
    CHECK(fields.size() == 5);
    CHECK(field(fields, "Expression") == "悩む");
    CHECK(field(fields, "Reading") == "なやむ");
    CHECK(field(fields, "Meaning").starts_with("<div style=\"text-align: left;\" class=\"yomitan-glossary\">"));
    CHECK(field(fields, "Meaning").ends_with("</div><br><br><a href=\"\"></a>"));
    CHECK(field(fields, "SentenceAudio") == "[sound:hearstash_1.wav]");
}

static void test_details(){
    AnkiSettings s;
    s.sentence_field = "Front";
    s.word_field = s.reading_field = "Back";

    // A highlight that isn't in the sentence highlights nothing
    CardContent card = CARD;
    card.highlight = "食べる";
    CHECK(field(format_card(card, s), "Front") == "私だってもうちょっと悩んでたかも");

    // Escaped, and line breaks kept
    card.sentence = "a < b & c\nd";
    card.highlight = "b";
    CHECK(field(format_card(card, s), "Front") ==
          "a &lt; <span style=\"font-size:1.3em; color: #00828a;\">b</span> &amp; c<br>d");

    // A kana-only word, as Yomitan writes it
    card.word = card.reading = "イチジク";
    CHECK(field(format_card(card, s), "Back") == "イチジク【イチジク】");

    // Nothing to put in a field leaves it out
    card.word = card.reading = "";
    CHECK(format_card(card, s).size() == 1);

    // Blank lines aren't senses
    s.meaning_field = "Back";
    card.meaning = "\n  3.  only one \n\n";
    const std::string back = field(format_card(card, s), "Back");
    CHECK(back.find("&quot;①&quot;;\">only one</li>") != std::string::npos);
    CHECK(back.find("②") == std::string::npos);
}

int main(){
    test_immersion_layout();
    test_a_field_each();
    test_details();
    if (failures == 0) std::puts("All card format tests passed");
    return failures;
}
