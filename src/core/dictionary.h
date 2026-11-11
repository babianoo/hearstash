#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// JSON
#include <nlohmann/json_fwd.hpp>

// The app's own dictionary file: a Yomitan-format dictionary such as
// Jitendex (import_dictionary() makes one from its zip) laid out so a word
// is found without reading the rest. The zip is 39 MB of JSON that's
// 544 MB unpacked and has to be read whole to find anything; this is about
// 30 MB and a lookup reads a few kilobytes of it.
//
// A file is made of sections, listed at its start:
//   - what the dictionary is: its title, revision, credits and stylesheet
//   - every word and reading in it, sorted, each with the rows it's in
//   - the rows: one per way of writing and reading a word
//   - the rows' definitions, as the JSON the dictionary has them in. Many
//     rows share one (every spelling of a word has the same), so each is
//     kept once, in blocks of about 64 KiB that are compressed together.
// All but the definitions are loaded when the file is opened; a lookup
// then reads and unpacks the one block its definitions are in.
//
// It holds the dictionary's text unchanged, only rearranged, so it's under
// the dictionary's own licence: for Jitendex, CC BY-SA 4.0. It isn't
// encrypted or tied to this app: this header is its whole description.

// What a dictionary says of itself, from the index.json in its zip
struct DictionaryInfo {
    // e.g. "Jitendex.org [2026-10-03]"
    std::string title;
    // e.g. "2026.10.03.0". A newer one sorts after an older one.
    std::string revision;
    std::string author;
    // Its home page
    std::string url;
    // Its own credits and licence, to show the user
    std::string attribution;
    // A small JSON file saying which revision is the latest, and where
    // that one's zip is. Empty if the dictionary doesn't say.
    std::string index_url;
    std::string download_url;
    // The styles.css in its zip, which a card needs to show its
    // definitions as intended. Empty if it has none.
    std::string styles;
    // When this file was made from the zip, in seconds since 1970
    std::int64_t converted = 0;
};

// One way of writing and reading a word, as a row of a term bank has it.
// The views are into the Dictionary it's from, and die with it.
struct DictionaryRow {
    // e.g. 獲物 and えもの. reading is empty when the word is written in
    // kana, so is its own reading.
    std::string_view term;
    std::string_view reading;
    // What the dictionary tags this row with, separated by spaces, e.g.
    // "★ priority form"
    std::string_view tags;
    // How it inflects, separated by spaces: "v1", "v5", "vs", "vk", "vz"
    // or "adj-i". Empty for a word that doesn't.
    std::string_view rules;
    // Higher for a more common word or spelling
    int score = 0;
    // The same for rows that are spellings of one word
    int sequence = 0;
    // Not empty for a row that only says the word is listed under another
    // spelling, e.g. 惡どい under 悪どい. It has no definitions of its own.
    std::string_view redirect;
    // Which row of the dictionary it is, in the zip's order
    std::uint32_t index = 0;
};

class Dictionary {
public:
    // Loads the file's lists. Throws std::runtime_error if it can't be
    // read or isn't a dictionary file of this version.
    explicit Dictionary(std::filesystem::path file);

    // Its rows hold views into it
    Dictionary(const Dictionary&) = delete;
    Dictionary& operator=(const Dictionary&) = delete;

    const DictionaryInfo& info() const { return info_; }
    const std::filesystem::path& path() const { return file; }
    size_t row_count() const { return rows.size() / ROW_SIZE; }

    // The rows whose term or reading is exactly key, in the zip's order.
    // Empty if there are none. Safe to call from several threads at once,
    // like definitions().
    std::vector<DictionaryRow> find(std::string_view key) const;

    // The row's definitions: the list a term bank row has, of text and
    // structured content (render_definitions() takes it as it is). An
    // empty list for a redirect. Throws std::runtime_error if the file
    // can't be read any more or is damaged.
    nlohmann::json definitions(const DictionaryRow& row) const;

    // Bytes of a stored row: see dictionary.cpp
    static constexpr size_t ROW_SIZE = 40;

private:
    std::filesystem::path file;
    DictionaryInfo info_;
    // The sections, as they are in the file once unpacked. The numbers in
    // them are read in place.
    std::string keys;       // Every key's bytes, sorted, one after another
    std::string key_ends;   // u32 per key: where it ends in keys
    std::string key_rows;   // u32 per key, and one more: where its rows start in row_list
    std::string row_list;   // u32 each: a row's index
    std::string rows;       // ROW_SIZE bytes each
    std::string text;       // Tags, rules and redirects, each ending in a 0
    std::string blocks;     // 16 bytes per block of definitions: where it is

    size_t key_count() const { return key_ends.size() / 4; }
    std::string_view key(size_t i) const;
    std::string_view text_at(std::uint32_t offset) const;
    DictionaryRow row(std::uint32_t index) const;
};

// Writes a dictionary file. Rows are added in the zip's order, then
// finish() writes the lists. The definitions go to the file as they're
// added, so a whole dictionary's never have to be in memory at once.
class DictionaryWriter {
public:
    // Starts the file, replacing one that's there. Throws
    // std::runtime_error if it can't be written, as add_row() and
    // finish() do.
    explicit DictionaryWriter(const std::filesystem::path& file);

    // definitions is the row's definition list as JSON text. For a row
    // that is listed under another spelling, redirect is that spelling and
    // definitions is ignored.
    void add_row(std::string_view term, std::string_view reading, std::string_view tags, std::string_view rules,
                 int score, int sequence, std::string_view definitions, std::string_view redirect = {});

    // Writes the rest. The file isn't a dictionary until this returns.
    void finish(const DictionaryInfo& info);

private:
    struct Row {
        std::string term, reading;
        std::uint32_t tags, rules, redirect;
        std::int32_t score, sequence;
        std::uint32_t block, offset, length;
    };
    struct Definition { std::uint32_t block, offset, length; };

    std::filesystem::path file;
    std::ofstream out;
    std::vector<Row> rows;
    // Tags, rules and redirects: a few thousand different ones among
    // hundreds of thousands of rows, so each is kept once
    std::string text{'\0'};
    std::unordered_map<std::string, std::uint32_t> text_offsets;
    // The definitions already written, by their SHA-1
    std::unordered_map<std::string, Definition> written;
    // The block being filled, and where the finished ones are
    std::string block;
    std::string blocks;
    std::uint32_t block_count = 0;

    std::uint32_t intern(std::string_view s);
    void end_block();
    void write(std::string_view bytes);
};
