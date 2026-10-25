#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

// Thrown for a non-2xx response, so callers can turn specific status codes
// into friendlier messages (e.g. DeepL's 456 "quota exceeded").
class HttpError : public std::runtime_error {
public:
    HttpError(long status, const std::string& what)
        : std::runtime_error(what), status(status) {}

    long status;
};

// Thrown by http_download when it's stopped, so a cancel isn't reported
// as a failure
class DownloadCancelled : public std::runtime_error {
public:
    DownloadCancelled() : std::runtime_error("Download cancelled") {}
};

// Called many times a second during a download, on its thread, with the
// bytes received so far and the size, which is 0 until the server sends
// it. Throwing stops the download and http_download rethrows it.
using DownloadProgress = std::function<void(std::uint64_t received, std::uint64_t total)>;

// Sets up libcurl. Call once in main(), before starting any threads.
void http_global_init();

// Blocking requests that return the response body. headers are full
// header lines, e.g. "Content-Type: application/json".
// Throw HttpError on a non-2xx status, std::runtime_error on network failure.
std::string http_get(const std::string& url);
std::string http_post(const std::string& url, const std::string& body,
                      const std::vector<std::string>& headers = {});

// Downloads url into file, replacing it, for files too big to hold in
// memory. Blocks until it's done, however long that takes: it only gives
// up when the connection stalls. Requesting stop on stop makes it throw
// DownloadCancelled. On any exception, file may hold part of the download.
// Throws HttpError on a non-2xx status, std::runtime_error on network
// failure or if file can't be written.
void http_download(const std::string& url, const std::filesystem::path& file,
                   const DownloadProgress& on_progress = {}, std::stop_token stop = {});

// Percent-encodes text for use in a URL query, e.g. 兄弟 -> %E5%85%84%E5%BC%9F
std::string url_encode(std::string_view text);
