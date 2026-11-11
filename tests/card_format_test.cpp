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
    .word = "悩む", .reading = "なやむ", .meaning = "[Godan verb with 'mu' ending] [Intransitive verb]\n1. to be worried; to be troubled\n2. to suffer (from)",
    .glossary_html = {},
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
          "<li data-dictionary=\"Jisho.org\"><span><ul data-sc-content=\"sense-groups\">"
          "<li data-sc-content=\"sense-group\">"
          "<span data-sc-class=\"tag\" data-sc-content=\"part-of-speech-info\">Godan verb with 'mu' ending</span>"
          "<span data-sc-class=\"tag\" data-sc-content=\"part-of-speech-info\">Intransitive verb</span>"
          "<ol>"
          "<li data-sc-content=\"sense\" style=\"list-style-type: &quot;①&quot;;\">"
          "<ul data-sc-content=\"glossary\"><li>to be worried</li><li>to be troubled</li></ul></li>"
          "<li data-sc-content=\"sense\" style=\"list-style-type: &quot;②&quot;;\">"
          "<ul data-sc-content=\"glossary\"><li>to suffer (from)</li></ul></li>"
          "</ol></li>"
          "</ul></span></li></ol></div><br><br><a href=\"\"></a><br>[sound:hearstash_1.wav]");
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
    // Not the defaults, which fill a field each
    AnkiSettings s;
    s.meaning_field = s.audio_field = s.picture_field = "";
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
    CHECK(back.find("&quot;①&quot;;\"><ul data-sc-content=\"glossary\"><li>only one</li></ul>") != std::string::npos);
    CHECK(back.find("②") == std::string::npos);
    // With no parts of speech, a group without badges
    CHECK(back.find("data-sc-class=\"tag\"") == std::string::npos);
}

// Senses grouped under their parts of speech, numbered through
static void test_sense_groups(){
    AnkiSettings s;
    s.meaning_field = "Meaning";
    CardContent card = CARD;
    card.meaning = "[Noun]\n1. paper\n2. sheet;; leaf ;\n[Unused]\n[Wikipedia definition]\n3. Paper <a>";
    const std::string meaning = field(format_card(card, s), "Meaning");

    const std::string noun =
        "<li data-sc-content=\"sense-group\">"
        "<span data-sc-class=\"tag\" data-sc-content=\"part-of-speech-info\">Noun</span><ol>"
        "<li data-sc-content=\"sense\" style=\"list-style-type: &quot;①&quot;;\">"
        "<ul data-sc-content=\"glossary\"><li>paper</li></ul></li>"
        // Empty glosses are left out
        "<li data-sc-content=\"sense\" style=\"list-style-type: &quot;②&quot;;\">"
        "<ul data-sc-content=\"glossary\"><li>sheet</li><li>leaf</li></ul></li>"
        "</ol></li>";
    const std::string wikipedia =
        "<li data-sc-content=\"sense-group\">"
        "<span data-sc-class=\"tag\" data-sc-content=\"part-of-speech-info\">Wikipedia definition</span><ol>"
        "<li data-sc-content=\"sense\" style=\"list-style-type: &quot;③&quot;;\">"
        "<ul data-sc-content=\"glossary\"><li>Paper &lt;a&gt;</li></ul></li>"
        "</ol></li>";
    CHECK(meaning.find(noun + wikipedia) != std::string::npos);
    // Parts of speech with no senses under them aren't a group
    CHECK(meaning.find("Unused") == std::string::npos);

    // Only parts of speech is no glossary at all
    card.meaning = "[Noun]";
    CHECK(field(format_card(card, s), "Meaning") == "(missing)");
}

// A dictionary's own entry goes on the card in place of the typed meaning
static void test_dictionary_entry(){
    AnkiSettings s;
    s.word_field = s.reading_field = s.meaning_field = "Back";
    s.sentence_field = s.audio_field = s.picture_field = "";
    CardContent card = CARD;
    card.glossary_html = card_glossary("Dict [1] \"x\"", "<ul><li>worry</li></ul>", "li { color: red; }");
    CHECK(card.glossary_html ==
          "<div style=\"text-align: left;\" class=\"yomitan-glossary\"><ol><li data-dictionary=\"Dict [1] &quot;x&quot;\">"
          "<span><ul><li>worry</li></ul></span></li></ol><style>li { color: red; }</style></div>");
    CHECK(field(format_card(card, s), "Back") == "悩む【なやむ】 <br>" + card.glossary_html + "<br><br><a href=\"\"></a>");

    // Nothing to style, or something that isn't styles: no <style>
    CHECK(card_glossary("D", "x", "") == "<div style=\"text-align: left;\" class=\"yomitan-glossary\"><ol>"
                                         "<li data-dictionary=\"D\"><span>x</span></li></ol></div>");
    CHECK(card_glossary("D", "x", "</style><script>").find("script") == std::string::npos);
    // No entry is no glossary
    CHECK(card_glossary("D", "", "li {}").empty());
}

int main(){
    test_immersion_layout();
    test_dictionary_entry();
    test_a_field_each();
    test_details();
    test_sense_groups();
    if (failures == 0) std::puts("All card format tests passed");
    return failures;
}
