#pragma once

#include <functional>
#include <string>
#include <string_view>

// JSON
#include <nlohmann/json_fwd.hpp>

// Turns the definitions of a dictionary in Yomitan's format, such as
// Jitendex, into HTML for an Anki card, so a card made here looks like one
// Yomitan made from the same dictionary.
//
// Such a dictionary writes a definition as "structured content": text,
// lists of content, and elements like
//   {"tag": "span", "data": {"content": "part-of-speech-info"},
//    "title": "noun (common)", "content": "noun"}
// which becomes
//   <span data-sc-content="part-of-speech-info" title="noun (common)">noun</span>
// The data-sc-* attributes are what the dictionary's own stylesheet looks
// for, so they have to be spelled the way it expects.
//
// Written from the format's published schema (dictionary-term-bank-v3 in
// the Yomitan repository) and the HTML a dictionary's stylesheet expects.
// It shares no code with Yomitan, which is GPL.

struct ContentOptions {
    // Given the path of an image in the dictionary's archive, says what to
    // put in the <img>'s src: its name in Anki's media folder, once the
    // caller has stored it there. Images are left out if this isn't set or
    // returns nothing.
    std::function<std::string(std::string_view path)> image_src;
};

// One piece of structured content as HTML. Text is escaped and only the
// format's own elements, attributes and style properties are written, so
// nothing in a dictionary can add a script to a card. What isn't in the
// format is left out: an element with an unknown tag leaves just what's
// inside it.
std::string render_content(const nlohmann::json& content, const ContentOptions& options = {});

// A term's definitions as HTML: the sixth item of a row in a term bank, a
// list in which each item is plain text, {"type": "text"}, {"type":
// "structured-content"} or {"type": "image"}. One definition is given as it
// is, several as a <ul>. Empty if there are none.
std::string render_definitions(const nlohmann::json& definitions, const ContentOptions& options = {});

// A dictionary's stylesheet (the styles.css in its archive), with every
// rule made to apply only inside
//   <div class="yomitan-glossary"> ... <li data-dictionary="dictionary">
// which is where a card has that dictionary's definitions. So it can go
// in a <style> on the card, or in the note type, without touching the rest
// of the card or another dictionary's definitions. @import and the like
// are dropped: a card shouldn't fetch anything.
std::string scope_styles(std::string_view css, std::string_view dictionary);
