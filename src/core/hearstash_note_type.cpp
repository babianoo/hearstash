#include "core/hearstash_note_type.h"

#include <algorithm>
#include <span>
#include <string_view>

#include "core/anki_connect.h"

static constexpr const char* CARD_NAME = "Card 1";

// The sentence, or the word when there's none, since Anki doesn't make a
// card whose front is empty. The highlight's span comes with the sentence.
static constexpr const char* FRONT =
    "{{#Sentence}}{{Sentence}}{{/Sentence}}{{^Sentence}}{{Word}}{{/Sentence}}\n";

// Laid out like the user's Yomitan cards: 悩む【なやむ】, the glossary
// (which format_card follows with Yomitan's empty {url} link), then the
// sentence's audio with the picture next to it
static constexpr const char* BACK =
    "{{FrontSide}}\n"
    "\n"
    "<hr id=answer>\n"
    "\n"
    "{{Word}}{{#Reading}}【{{Reading}}】{{/Reading}}<br>\n"
    "{{Meaning}}\n"
    "{{#Audio}}<br>{{Audio}}{{/Audio}}\n"
    "{{Picture}}\n";

// Anki's own Basic style, in a Japanese font and as big as the user's
// Yomitan cards. The glossary is smaller and left-aligned (format_card
// aligns it), and styled to look like Jitendex's in Yomitan: a mark in
// front of each group of senses, their parts of speech as grey badges,
// and each gloss on a line of its own. A picture never runs off the side.
static constexpr const char* CSS =
    ".card {\n"
    "    font-family: \"Noto Sans CJK JP\", \"Noto Sans JP\", \"Yu Gothic\", \"Meiryo\", \"Hiragino Sans\", sans-serif;\n"
    "    font-size: 44px;\n"
    "    line-height: 1.5;\n"
    "    text-align: center;\n"
    "    color: black;\n"
    "    background-color: white;\n"
    "}\n"
    ".yomitan-glossary {\n"
    "    font-size: 24px;\n"
    "}\n"
    ".yomitan-glossary ul[data-sc-content=\"sense-groups\"] {\n"
    "    list-style-type: \"＊\";\n"
    "}\n"
    ".yomitan-glossary li[data-sc-content=\"sense-group\"] {\n"
    "    padding-left: 0.25em;\n"
    "}\n"
    ".yomitan-glossary li[data-sc-content=\"sense-group\"] + li[data-sc-content=\"sense-group\"] {\n"
    "    margin-top: 0.5em;\n"
    "}\n"
    ".yomitan-glossary span[data-sc-class=\"tag\"] {\n"
    "    border-radius: 0.3em;\n"
    "    padding: 0.2em 0.3em;\n"
    "    margin-right: 0.5em;\n"
    "    font-size: 0.8em;\n"
    "    font-weight: bold;\n"
    "    vertical-align: text-bottom;\n"
    "    color: white;\n"
    "    background-color: #565656;\n"
    "}\n"
    ".yomitan-glossary li[data-sc-content=\"sense\"] {\n"
    "    padding-left: 0.25em;\n"
    "}\n"
    ".yomitan-glossary ul[data-sc-content=\"glossary\"] {\n"
    "    list-style-type: none;\n"
    "    padding-left: 0.25em;\n"
    "}\n"
    "img {\n"
    "    max-width: 100%;\n"
    "    height: auto;\n"
    "}\n";

// What the app gave the note type before, so one still like that can be
// told from one the user changed
static constexpr const char* OLD_BACKS[] = {
    "{{FrontSide}}\n"
    "\n"
    "<hr id=answer>\n"
    "\n"
    "{{Word}}{{#Reading}}【{{Reading}}】{{/Reading}}<br>\n"
    "{{Meaning}}\n"
    "{{#Audio}}<br>{{Audio}}{{/Audio}}\n"
    "{{#Picture}}<div>{{Picture}}</div>{{/Picture}}\n",
};
static constexpr const char* OLD_CSS[] = {
    ".card {\n"
    "    font-family: \"Noto Sans CJK JP\", \"Noto Sans JP\", \"Yu Gothic\", \"Meiryo\", \"Hiragino Sans\", sans-serif;\n"
    "    font-size: 34px;\n"
    "    line-height: 1.5;\n"
    "    text-align: center;\n"
    "    color: black;\n"
    "    background-color: white;\n"
    "}\n"
    ".yomitan-glossary {\n"
    "    font-size: 22px;\n"
    "}\n"
    "img {\n"
    "    max-width: 100%;\n"
    "    height: auto;\n"
    "}\n",
};

// Whether text is one of olds, give or take the blank space around it,
// which Anki may not keep
static bool is_one_of(std::string_view text, std::span<const char* const> olds){
    const auto trimmed = [](std::string_view s){
        const char* blank = " \t\r\n";
        const size_t first = s.find_first_not_of(blank);
        return first == std::string_view::npos ? std::string_view() : s.substr(first, s.find_last_not_of(blank) - first + 1);
    };
    return std::ranges::any_of(olds, [&](const char* old){ return trimmed(text) == trimmed(old); });
}

void ensure_hearstash_note_type(){
    const std::vector<std::string> names = anki_model_names();
    if (std::ranges::find(names, HEARSTASH_NOTE_TYPE) == names.end()){
        anki_create_model(HEARSTASH_NOTE_TYPE, hearstash_fields(), CSS, {{.name = CARD_NAME, .front = FRONT, .back = BACK}});
        return;
    }

    // One from an older version of the app is brought up to date, each
    // half only if it's still as the app made it
    if (is_one_of(anki_model_css(HEARSTASH_NOTE_TYPE), OLD_CSS)) anki_update_model_css(HEARSTASH_NOTE_TYPE, CSS);
    for (const AnkiCardTemplate& card : anki_model_templates(HEARSTASH_NOTE_TYPE)){
        // The front hasn't changed between versions, so it's kept
        if (card.name == CARD_NAME && is_one_of(card.back, OLD_BACKS))
            anki_update_model_templates(HEARSTASH_NOTE_TYPE, {{.name = CARD_NAME, .front = card.front, .back = BACK}});
    }
}
