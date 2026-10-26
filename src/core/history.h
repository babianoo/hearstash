#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/api.h"

// The search history, saved as JSON, newest search first:
//
//   {"entries": [{"query": "兄弟", "api": "jisho", "time": 1793012345},
//                {"query": "...", "api": "deepl", "time": 1793012000,
//                 "translation": "..."}],
//    "version": 1}

// DeepL translations longer than this many characters aren't kept with
// their search, so they don't make the file huge. Searching one of those
// again asks DeepL again.
inline constexpr size_t MAX_CACHED_TRANSLATION = 5000;

// One search
struct HistoryEntry {
    // As it was typed, in full
    std::string query;
    Api api = Api::Jisho;
    // When, as Unix time: seconds since 1970-01-01 UTC
    std::int64_t time = 0;
    // What DeepL returned, so searching it again doesn't use up the
    // monthly quota. Only for DeepL, and only if it's cacheable().
    std::optional<std::string> translation;
};

// Whether a translation is short enough to keep. Counts characters, not
// bytes: a Japanese character takes 3 bytes in UTF-8.
bool cacheable(std::string_view translation);

// Never throws: without a file, e.g. on the first run, the history is
// empty. So it is with a file that isn't JSON, which is renamed to
// <file>.bad first. An entry that's broken (e.g. edited by hand) is
// skipped, and the rest are kept.
std::vector<HistoryEntry> load_history(const std::filesystem::path& file);

// entries newest first. Creates file's folder if needed. The old file is
// only replaced once the new one is completely written. Throws
// std::runtime_error or std::filesystem::filesystem_error.
void save_history(const std::filesystem::path& file, const std::vector<HistoryEntry>& entries);
