#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <stop_token>

#include "core/dictionary.h"

// Called as an import goes, on its thread, with how many of the zip's
// term banks are done and how many there are
using ImportProgress = std::function<void(std::size_t done, std::size_t total)>;

// Makes the app's dictionary file (dictionary.h) from a dictionary in
// Yomitan's format, as its zip is downloaded: index.json, term_bank_*.json
// and, if it has one, styles.css. Its pictures aren't taken along.
//
// For Jitendex that's reading 544 MB of JSON, which takes about half a
// minute, so not on the UI thread. It's written to <file>.part and only
// renamed to file once complete, replacing a file that's there, so a file
// with the real name is always whole. Requesting stop makes it throw
// DownloadCancelled (http.h), as an import is the second half of an
// update. If it throws, the .part file is deleted and file is left as it
// was.
//
// Returns what the dictionary says of itself. Throws std::runtime_error if
// the zip is damaged or isn't such a dictionary, or
// std::filesystem::filesystem_error.
DictionaryInfo import_dictionary(const std::filesystem::path& zip, const std::filesystem::path& file,
                                 const ImportProgress& on_progress = {}, std::stop_token stop = {});
