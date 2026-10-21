#pragma once

#include <string>
#include <string_view>

// Blocking HTTP GET that returns the response body.
// Throws std::runtime_error on network failure or a non-2xx status.
std::string http_get(const std::string& url);

// Percent-encodes text for use in a URL query, e.g. 兄弟 -> %E5%85%84%E5%BC%9F
std::string url_encode(std::string_view text);
