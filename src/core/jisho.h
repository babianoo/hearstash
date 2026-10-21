#pragma once

#include <string>
#include <string_view>
#include <vector>

// One way of writing the word, e.g. 兄弟 / きょうだい.
// word is empty for kana-only words, which only have a reading.
struct JishoForm {
    std::string word;
    std::string reading;
};

// One meaning, e.g. "brothers and sisters; siblings" [Noun]
struct JishoSense {
    std::vector<std::string> definitions;
    std::vector<std::string> parts_of_speech;
};

// One dictionary word. A search returns several of these, best match first,
// e.g. きょうだい gives 兄弟, 強大, 鏡台...
struct JishoEntry {
    std::vector<JishoForm> forms;
    std::vector<JishoSense> senses;
    bool is_common = false;
};

// Searches jisho.org for keyword (kanji, kana or romaji).
// Returns an empty vector when nothing matches.
// Blocks until the request finishes, so don't call it on the UI thread.
// Throws std::runtime_error on network failure or a malformed response.
std::vector<JishoEntry> jisho_search(std::string_view keyword);
