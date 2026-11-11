#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>

#include "core/dictionary.h"
#include "core/dictionary_import.h"
#include "core/http.h"

// Where the app's dictionary file (dictionary.h) is, how old it is, and
// getting a newer one.
//
// A build of the app ships with one, next to the program. The dictionary
// it's made from, Jitendex, comes out about monthly, and the licence of
// the data it's built on (JMdict's, from EDRDG) asks for a way to keep it
// current, so the app can also fetch the latest zip and convert it itself.
// That copy goes in data_dir()/dictionary, and is used when it's the newer
// of the two.

inline constexpr const char* DICTIONARY_FILE_NAME = "jitendex.hsdict";
// Where to ask for the latest revision when no dictionary is installed to
// say where. Jitendex's own, as its index.json gives it.
inline constexpr const char* DICTIONARY_INDEX_URL = "https://jitendex.org/static/yomitan.json";
// How old the dictionary gets before the app looks for a newer one
// without being asked
inline constexpr int DICTIONARY_MAX_AGE_DAYS = 30;

// The one shipped with the app: next to the program, or, installed the
// Unix way with the program in bin, in share/hearstash beside that. The
// first of those there is, or the first if there's neither. Throws
// std::runtime_error if the program's folder can't be found.
std::filesystem::path shipped_dictionary_path();
// The one an update writes. Throws std::runtime_error if data_dir() can't
// be found. Creates nothing.
std::filesystem::path updated_dictionary_path();

// Opens the newer of the two, or the one there is. nullptr if there's
// none, or neither can be read: a missing dictionary isn't an error, the
// app then looks words up online.
std::shared_ptr<const Dictionary> open_installed_dictionary();

// How many days ago the dictionary's revision came out, going by its
// revision, e.g. "2026.10.03.0", or failing that by when the file was
// made. 0 for one from the future.
int dictionary_age_days(const DictionaryInfo& info,
                        std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

struct DictionaryRelease {
    std::string title;      // e.g. "Jitendex.org [2026-11-01]"
    std::string revision;
    std::string download_url;
};

// Asks the dictionary's home which revision is the latest: installed says
// where to ask, or nullptr for DICTIONARY_INDEX_URL. A small request, but
// it blocks, so not on the UI thread. Throws std::runtime_error if it
// can't be reached or doesn't answer as expected.
DictionaryRelease latest_dictionary_release(const DictionaryInfo* installed);

// Whether release is newer than what's installed, or anything at all when
// nothing is
bool is_newer(const DictionaryRelease& release, const DictionaryInfo* installed);

// Downloads release's zip and converts it to updated_dictionary_path(),
// replacing the one there. Blocks for about a minute, so not on the UI
// thread. The zip is deleted afterwards. Call reload_dictionary() once
// it's done for lookups to use it. Requesting stop makes it throw
// DownloadCancelled. If it throws, the dictionary there was is left as it
// was. Throws what http_download and import_dictionary throw.
DictionaryInfo update_dictionary(const DictionaryRelease& release, const DownloadProgress& on_download = {},
                                 const ImportProgress& on_import = {}, std::stop_token stop = {});
