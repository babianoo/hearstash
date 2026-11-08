#pragma once

#include <filesystem>
#include <stop_token>
#include <string>
#include <string_view>

#include "core/http.h"

// Downloads url to file, creating file's folder if needed and replacing a
// file that's there, as the models are downloaded. It's written to
// <file>.part, and only renamed to file once complete and its SHA-1 is
// sha1, so a file with the real name is always whole and undamaged.
// Blocks until done, so not on the UI thread. on_progress and stop are
// http_download's; checking the SHA-1 takes a few seconds for a large
// file, with no progress reported but stop still checked.
// If it throws, the .part file is deleted and file is left as it was.
// Throws what http_download throws, std::runtime_error if the SHA-1
// doesn't match, or std::filesystem::filesystem_error.
void download_verified(const std::string& url, const std::filesystem::path& file, std::string_view sha1,
                       const DownloadProgress& on_progress = {}, std::stop_token stop = {});

// Where download_verified writes file until it's complete
std::filesystem::path partial_path(const std::filesystem::path& file);
