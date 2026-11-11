#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/settings.h"

// What goes on a card, as the user left it in the card window
struct CardContent {
    std::string word;       // The dictionary form, e.g. 悩む
    std::string reading;    // e.g. なやむ
    // One sense a line, its glosses separated by ';', e.g.
    // "1. prey; catch; kill". A "1. " in front of a line is dropped: the
    // glossary numbers them itself. A line like "[Noun] [Suru verb]" gives
    // the parts of speech of the senses under it, up to the next such line.
    std::string meaning;
    // The meanings as a dictionary file has them, already HTML
    // (card_glossary()). When it isn't empty it goes on the card in
    // meaning's place.
    std::string glossary_html;
    std::string sentence;
    // The part of sentence to highlight, as it's written there, e.g. 悩んでた.
    // Its first place in sentence is highlighted; nothing is if it isn't
    // in it.
    std::string highlight;
    // As stored in Anki's media folder. Empty for none.
    std::string audio_file;
    std::string picture_file;
};

// The card's fields as HTML, each in the field Settings > Anki says, made
// to look like the user's Yomitan cards (their template is
// "{cloze-prefix}<span style=...>{cloze-body}</span>{cloze-suffix}" on the
// front and "{expression}【{reading}】{audio} {pitch-accents}<br>
// {glossary-brief}<br>{conjugation}<br>{url}" on the back):
//
// - the sentence, with the highlight in a bigger teal span
// - the word and its reading as 悩む【なやむ】 when they share a field
// - the meanings as Yomitan's glossary, laid out like Jitendex's: in a
//   left-aligned "yomitan-glossary" div, the senses numbered ①②③ under
//   their parts of speech, each gloss on a line of its own, followed by
//   the template's empty {conjugation} and {url}
// - the audio as [sound:...] and the picture as <img alt="snapshot">,
//   like mpvacious adds them
//
// Parts sharing a field come in that order, each on a new line. Text is
// escaped, so <, > and & show as they are. Fields with nothing in them
// are left out.
std::vector<std::pair<std::string, std::string>> format_card(const CardContent& card, const AnkiSettings& settings);

// A dictionary's entry for a word as the HTML a card's meaning gets, the
// way Yomitan writes it:
//   <div class="yomitan-glossary"><ol><li data-dictionary="title">
//     <span>definitions</span></li></ol><style>styles</style></div>
// definitions is render_definitions()'s HTML, and styles the dictionary's
// stylesheet from scope_styles() for the same title, which goes along so
// the entry looks right on any note type. Empty if definitions is.
std::string card_glossary(std::string_view dictionary, std::string_view definitions, std::string_view styles);
