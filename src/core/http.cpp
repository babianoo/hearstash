#include "core/http.h"

#include <memory>
#include <stdexcept>
#include <format>

// CURL
#include <curl/curl.h>

// Some APIs reject requests without one
static constexpr const char* USER_AGENT = "prototype/0.1";

// unique_ptr with curl's own cleanup function instead of delete, so the
// handle is freed however we leave the function (return or throw).
using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;

static CurlHandle make_handle(){
    CurlHandle curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("curl_easy_init failed");
    return curl;
}

// libcurl hands the body over in chunks; append each one to the string.
// Returning anything other than the chunk's size makes curl abort.
static size_t write_callback(char* data, size_t size, size_t nmemb, void* out){
    static_cast<std::string*>(out)->append(data, size * nmemb);
    return size * nmemb;
}

void http_global_init(){
    // Otherwise the first curl_easy_init() does it, which isn't safe if two
    // threads get there at once. Never cleaned up: search threads may
    // still be running when main() returns, and the OS frees it anyway.
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        throw std::runtime_error("curl_global_init failed");
}

std::string http_get(const std::string& url){
    CurlHandle curl = make_handle();
    std::string body;

    // libcurl is a C library, so it takes C strings
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body);

    CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) throw std::runtime_error(curl_easy_strerror(res));

    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status < 200 || status >= 300)
        throw std::runtime_error(std::format("HTTP {} from {}", status, url));

    return body;
}

std::string url_encode(std::string_view text){
    CurlHandle curl = make_handle();
    char* encoded = curl_easy_escape(curl.get(), text.data(), static_cast<int>(text.size()));
    if (!encoded) throw std::runtime_error("curl_easy_escape failed");

    std::string result(encoded);
    curl_free(encoded);
    return result;
}
