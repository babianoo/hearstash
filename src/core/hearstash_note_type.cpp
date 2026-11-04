#include "core/hearstash_note_type.h"

#include <algorithm>

#include "core/anki_connect.h"

// The sentence, or the word when there's none, since Anki doesn't make a
// card whose front is empty. The highlight's span comes with the sentence.
static constexpr const char* FRONT =
    "{{#Sentence}}{{Sentence}}{{/Sentence}}{{^Sentence}}{{Word}}{{/Sentence}}\n";

// Laid out like the user's Yomitan cards: 悩む【なやむ】, the glossary
// (which format_card follows with Yomitan's empty {url} link), then the
// sentence's audio, then the picture
static constexpr const char* BACK =
    "{{FrontSide}}\n"
    "\n"
    "<hr id=answer>\n"
    "\n"
    "{{Word}}{{#Reading}}【{{Reading}}】{{/Reading}}<br>\n"
    "{{Meaning}}\n"
    "{{#Audio}}<br>{{Audio}}{{/Audio}}\n"
    "{{#Picture}}<div>{{Picture}}</div>{{/Picture}}\n";

// Anki's own Basic style, in a Japanese font and bigger. The glossary is
// smaller and left-aligned (format_card aligns it), and a picture never
// runs off the side.
static constexpr const char* CSS =
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
    "}\n";

void ensure_hearstash_note_type(){
    const std::vector<std::string> names = anki_model_names();
    if (std::ranges::find(names, HEARSTASH_NOTE_TYPE) != names.end()) return;
    anki_create_model(HEARSTASH_NOTE_TYPE, hearstash_fields(), CSS, {{.name = "Card 1", .front = FRONT, .back = BACK}});
}
