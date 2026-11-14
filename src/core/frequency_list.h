#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "core/http.h"

// How common words are, from a frequency dictionary in Yomitan's format,
// shown beside a word the way Yomitan shows it: the dictionary's name, then
// what it has for that spelling and reading, e.g.
//
//   JPDBv2㋕  312, 19896㋕
//
// for 読む. That's JPDBv2㋕ (Kuuube's build of jpdb.io's frequency list): a
// word's rank among everything jpdb.io has read, lower being more common,
// and with ㋕ its rank when it's written in kana.
//
// Unlike Jitendex it isn't shipped with the app: its data's licence isn't
// stated, so it's only downloaded, from where its author publishes it, when
// the user asks for it in Settings > Sources.
//
// The zip's term_meta_bank_*.json are about 30 MB of JSON. The app keeps
// what it needs from them in a file of its own:
//   "HSFREQ\0\0", u32 version, u32 size of the metadata, the metadata as
//   JSON (a FrequencyInfo's fields), u64 size of the rows unpacked, then
//   the rows deflated with zlib: one per line, "term\treading\tshown\n",
//   sorted by term and otherwise in the zip's order. reading is empty for
//   a row that's for any reading of the term.
// About 4 MB, read whole when it's opened, in a fraction of a second.

struct FrequencyInfo {
    // e.g. "JPDBv2㋕", what's shown beside each word
    std::string title;
    // e.g. "JPDB v2.2 Frequency Kana 2024-10-13"
    std::string revision;
    std::string author;
    std::string url;
    std::string attribution;
    std::string description;
    // When the file was made from the zip, in seconds since 1970
    std::int64_t converted = 0;
};

class FrequencyList {
public:
    // Reads the file. Throws std::runtime_error if it can't be read or
    // isn't a frequency file of this version.
    explicit FrequencyList(const std::filesystem::path& file);

    FrequencyList(const FrequencyList&) = delete;
    FrequencyList& operator=(const FrequencyList&) = delete;

    const FrequencyInfo& info() const { return info_; }
    size_t size() const { return starts.size(); }

    // What the list has for term read as reading, as it's to be shown, in
    // the list's order, e.g. {"312", "19896㋕"} for 読む / よむ. Like
    // Yomitan: the rows for that term with that reading, and those for the
    // term with no reading given. A word written in kana is its own
    // reading. Empty if the list doesn't have it. Safe from any thread.
    std::vector<std::string> find(std::string_view term, std::string_view reading) const;

private:
    FrequencyInfo info_;
    std::string rows;
    // Where each row starts in rows
    std::vector<std::uint32_t> starts;

    std::string_view term_at(size_t i) const;
};

// Makes a frequency file from a frequency dictionary's zip in Yomitan's
// format: its index.json and term_meta_bank_*.json, whose "freq" rows are
// kept and anything else (pitch accents, readings) left out. Written to
// <file>.part and renamed to file once whole, replacing a file that's
// there. Requesting stop makes it throw DownloadCancelled. Throws
// std::runtime_error if the zip isn't such a dictionary.
FrequencyInfo import_frequencies(const std::filesystem::path& zip, const std::filesystem::path& file,
                                 std::stop_token stop = {});

// ---- The app's frequency list ----

// The one the app uses: JPDBv2㋕, from Kuuuube's collection of Yomitan
// dictionaries. The file is fixed (a new revision gets a new name), so it's
// checked against its SHA-1.
inline constexpr const char* FREQUENCY_TITLE = "JPDBv2㋕";
inline constexpr const char* FREQUENCY_URL =
    "https://github.com/Kuuuube/yomitan-dictionaries/raw/main/dictionaries/JPDB_v2.2_Frequency_Kana_2024-10-13.zip";
inline constexpr const char* FREQUENCY_SHA1 = "79f8b85354ad49c20cc17752b8f6feaaa245aa71";
inline constexpr const char* FREQUENCY_FILE_NAME = "frequencies.hsfreq";

// data_dir()/dictionary/frequencies.hsfreq. Throws std::runtime_error if
// data_dir() can't be found. Creates nothing.
std::filesystem::path frequency_path();

// The installed list, opened the first time it's asked for. nullptr if
// there's none or it can't be read. Safe from any thread.
std::shared_ptr<const FrequencyList> current_frequencies();
// Makes current_frequencies() look again, after one was downloaded or
// deleted. Lookups already running keep the one they have.
void reload_frequencies();

// Downloads FREQUENCY_URL, checks it and converts it to frequency_path(),
// then reload_frequencies(). About 6 MB, and a few seconds to convert, so
// not on the UI thread. The zip is deleted afterwards. If it throws, the
// list there was is left as it was. Throws what download_verified() and
// import_frequencies() throw.
FrequencyInfo download_frequencies(const DownloadProgress& on_download = {}, std::stop_token stop = {});

// Deletes the installed list, then reload_frequencies(). Throws
// std::filesystem::filesystem_error if it can't be deleted.
void delete_frequencies();
