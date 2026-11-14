#pragma once

#include <string>
#include <vector>

// A word as a dictionary has it, whichever one it came from: jisho.org
// (jisho.h) or the dictionary file on this computer (word_lookup.h).

// One way of writing the word, e.g. 兄弟 / きょうだい.
// word is empty for kana-only words, which only have a reading.
struct WordForm {
    std::string word;
    std::string reading;
};

// One meaning, e.g. "brothers and sisters; siblings" [Noun]
struct WordSense {
    std::vector<std::string> definitions;
    std::vector<std::string> parts_of_speech;
};

// One dictionary word. A search returns several of these, best match first,
// e.g. きょうだい gives 兄弟, 強大, 鏡台...
struct WordEntry {
    std::vector<WordForm> forms;
    std::vector<WordSense> senses;
    bool is_common = false;
    // Where it's from: "jisho.org", or the dictionary file's title, e.g.
    // "Jitendex.org [2026-10-03]"
    std::string source;
    // From a dictionary file only: its whole entry as the HTML a card's
    // meaning gets (card_glossary()), with what the plain senses above
    // leave out, like example sentences and where they're from. Empty
    // from jisho.org.
    std::string glossary_html;
    // How common its first form is, from the frequency list
    // (frequency_list.h) if one is installed: the list's name, e.g.
    // "JPDBv2㋕", and what it has for the form, e.g. {"312", "19896㋕"}.
    // Both empty if there's no list or it doesn't have the word.
    std::string frequency_source;
    std::vector<std::string> frequencies;
};
