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

// curl_slist is curl's linked list of header lines
using CurlHeaders = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;

// Sends the request set up on curl and returns the body.
static std::string perform(CURL* curl, const std::string& url){
    std::string body;

    // libcurl is a C library, so it takes C strings
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) throw std::runtime_error(curl_easy_strerror(res));

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (status < 200 || status >= 300)
        throw HttpError(status, std::format("HTTP {} from {}", status, url));

    return body;
}

std::string http_get(const std::string& url){
    CurlHandle curl = make_handle();
    return perform(curl.get(), url);
}

std::string http_post(const std::string& url, const std::string& body,
                      const std::vector<std::string>& headers){
    CurlHandle curl = make_handle();

    CurlHeaders list(nullptr, curl_slist_free_all);
    for (const std::string& header : headers){
        // Returns the list's head: a new node the first time, the same
        // one after that. On failure the old list is left as it was.
        curl_slist* head = curl_slist_append(list.get(), header.c_str());
        if (!head) throw std::runtime_error("curl_slist_append failed");
        list.release();
        list.reset(head);
    }

    // Setting a body makes it a POST. curl copies neither, so body and
    // list have to live until perform() returns.
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, list.get());
    return perform(curl.get(), url);
}

std::string url_encode(std::string_view text){
    CurlHandle curl = make_handle();
    char* encoded = curl_easy_escape(curl.get(), text.data(), static_cast<int>(text.size()));
    if (!encoded) throw std::runtime_error("curl_easy_escape failed");

    std::string result(encoded);
    curl_free(encoded);
    return result;
}
