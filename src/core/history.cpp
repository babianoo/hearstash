#include "core/history.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

#include "core/atomic_write.h"
#include "core/bad_file.h"

// JSON
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

// Written into the file. Bump it when a field is renamed or changes
// meaning, so load_history can tell an older file apart and convert it.
static constexpr int VERSION = 1;

// Saved by its id, not its number, so reordering Api can't change what
// an old entry says. Not NLOHMANN_JSON_SERIALIZE_ENUM: that turns an id it
// doesn't know (e.g. from a newer version) into Jisho without a word.
static void to_json(json& j, Api api){
    j = API_IDS[static_cast<size_t>(api)];
}

static void from_json(const json& j, Api& api){
    const auto id = j.get<std::string>();
    const auto found = std::ranges::find(API_IDS, id);
    if (found == std::end(API_IDS)) throw std::runtime_error(std::format("unknown API \"{}\"", id));
    api = static_cast<Api>(found - std::begin(API_IDS));
}

// Not the NLOHMANN_DEFINE macros: they'd save a missing translation as
// null instead of leaving it out.
static void to_json(json& j, const HistoryEntry& entry){
    j = {{"query", entry.query}, {"api", entry.api}, {"time", entry.time}};
    if (entry.translation) j["translation"] = *entry.translation;
}

// Throws for a missing field or one of the wrong type
static void from_json(const json& j, HistoryEntry& entry){
    j.at("query").get_to(entry.query);
    from_json(j.at("api"), entry.api);
    j.at("time").get_to(entry.time);
    if (j.contains("translation")) entry.translation = j["translation"].get<std::string>();
}

bool cacheable(std::string_view translation){
    // A UTF-8 character is one byte that isn't 10xxxxxx, then any number
    // of bytes that are
    const auto chars = std::ranges::count_if(translation, [](char c){ return (c & 0xC0) != 0x80; });
    return static_cast<size_t>(chars) <= MAX_CACHED_TRANSLATION;
}

std::vector<HistoryEntry> load_history(const fs::path& file){
    std::ifstream in(file);
    if (!in) return {};

    json j;
    try {
        j = json::parse(in);
        if (!j.at("entries").is_array()) throw std::runtime_error("\"entries\" isn't a list");
    } catch (const std::exception& e){
        in.close();
        set_aside_bad_file(file, e.what(), "the history starts empty");
        return {};
    }

    std::vector<HistoryEntry> entries;
    int skipped = 0;
    for (const json& item : j["entries"]){
        try {
            HistoryEntry entry;
            from_json(item, entry);
            // Nothing to search again
            if (entry.query.empty()) throw std::runtime_error("empty query");
            // Only DeepL translates, and a long translation may have been
            // added by hand. Searching it again then asks DeepL.
            if (entry.api != Api::DeepL || (entry.translation && !cacheable(*entry.translation)))
                entry.translation.reset();
            entries.push_back(std::move(entry));
        } catch (const std::exception& e){
            if (skipped++ == 0)
                std::cerr << std::format("Skipping broken entries in {}, the first: {}\n", file.string(), e.what());
        }
    }
    if (skipped > 0) std::cerr << std::format("{} skipped in all.\n", skipped);
    return entries;
}

void save_history(const fs::path& file, const std::vector<HistoryEntry>& entries){
    json j = {{"entries", entries}, {"version", VERSION}};
    // replace: text that isn't valid UTF-8 (pasted from somewhere odd) is
    // saved with U+FFFD in place of the bad bytes, instead of throwing and
    // losing the whole history
    write_file_atomically(file, j.dump(4, ' ', false, json::error_handler_t::replace) + '\n');
}
