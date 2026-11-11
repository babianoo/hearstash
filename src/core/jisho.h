#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/word_entry.h"

// Searches jisho.org for keyword (kanji, kana or romaji).
// Returns an empty vector when nothing matches.
// Blocks until the request finishes, so don't call it on the UI thread.
// Throws std::runtime_error on network failure or a malformed response.
std::vector<WordEntry> jisho_search(std::string_view keyword);
