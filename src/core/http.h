#pragma once

#include <stdexcept>
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

// Sets up libcurl. Call once in main(), before starting any threads.
void http_global_init();

// Blocking requests that return the response body. headers are full
// header lines, e.g. "Content-Type: application/json".
// Throw HttpError on a non-2xx status, std::runtime_error on network failure.
std::string http_get(const std::string& url);
std::string http_post(const std::string& url, const std::string& body,
                      const std::vector<std::string>& headers = {});

// Percent-encodes text for use in a URL query, e.g. 兄弟 -> %E5%85%84%E5%BC%9F
std::string url_encode(std::string_view text);
