#include "core/http.h"

#include <exception>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <format>

// CURL
#include <curl/curl.h>

// Some APIs reject requests without one
static constexpr const char* USER_AGENT = "prototype/0.1";
// Hugging Face takes one, to its download servers. More than this is a
// loop, or a server that's gone wrong.
static constexpr long MAX_REDIRECTS = 5;

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

// The options every request sets. A redirect is only followed if
// follow_redirects, and only to HTTPS: one to http:// would send the
// request, e.g. the search, unencrypted.
static void set_common_options(CURL* curl, const std::string& url, bool follow_redirects){
    // libcurl is a C library, so it takes C strings
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, follow_redirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, MAX_REDIRECTS);
}

// Throws if the transfer failed or the status isn't 2xx
static void check_response(CURL* curl, CURLcode res, const std::string& url){
    // CURLE_HTTP_RETURNED_ERROR is CURLOPT_FAILONERROR stopping at the
    // status, which the check below reports
    if (res != CURLE_OK && res != CURLE_HTTP_RETURNED_ERROR)
        throw std::runtime_error(curl_easy_strerror(res));

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (status < 200 || status >= 300)
        throw HttpError(status, std::format("HTTP {} from {}", status, url));
}

// Sends the request set up on curl and returns the body.
static std::string perform(CURL* curl, const std::string& url, bool follow_redirects){
    std::string body;

    set_common_options(curl, url, follow_redirects);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

    CURLcode res = curl_easy_perform(curl);
    check_response(curl, res, url);
    return body;
}

// What http_download's callbacks share
struct Download {
    CURL* curl;
    std::ofstream out;
    const DownloadProgress& on_progress;
    std::stop_token stop;
    // What on_progress threw, rethrown once curl has returned: an
    // exception can't pass through curl's C code
    std::exception_ptr error;
};

// Like write_callback, but into the file. Returning less than the
// chunk's size makes curl abort with CURLE_WRITE_ERROR.
static size_t write_to_file(char* data, size_t size, size_t nmemb, void* d){
    auto* download = static_cast<Download*>(d);
    download->out.write(data, static_cast<std::streamsize>(size * nmemb));
    return download->out ? size * nmemb : 0;
}

// curl calls it as data arrives, and at least once a second while none
// does, so a stop is noticed even on a stalled connection. Returning
// non-zero makes curl abort with CURLE_ABORTED_BY_CALLBACK.
static int on_transfer_info(void* d, curl_off_t total, curl_off_t received, curl_off_t, curl_off_t){
    auto* download = static_cast<Download*>(d);
    if (download->stop.stop_requested()) return 1;
    if (!download->on_progress) return 0;
    // A redirect's own small body counts too, and would show as done
    // before the real file starts
    long status = 0;
    curl_easy_getinfo(download->curl, CURLINFO_RESPONSE_CODE, &status);
    if (status >= 300 && status < 400) return 0;

    try {
        download->on_progress(static_cast<std::uint64_t>(received), static_cast<std::uint64_t>(total));
    } catch (...){
        download->error = std::current_exception();
        return 1;
    }
    return 0;
}

std::string http_get(const std::string& url){
    CurlHandle curl = make_handle();
    return perform(curl.get(), url, true);
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
    // Not followed: DeepL doesn't redirect, and a 307 or 308 would send
    // the body, the text, on to wherever it points. One is reported as
    // an HttpError instead.
    return perform(curl.get(), url, false);
}

void http_download(const std::string& url, const std::filesystem::path& file,
                   const DownloadProgress& on_progress, std::stop_token stop){
    CurlHandle curl = make_handle();

    // Binary, or on Windows every \n byte would be written as \r\n
    Download download{curl.get(), std::ofstream(file, std::ios::binary), on_progress, std::move(stop), nullptr};
    if (!download.out) throw std::runtime_error(std::format("Couldn't open {}", file.string()));

    set_common_options(curl.get(), url, true);
    // No CURLOPT_TIMEOUT, since a big file takes minutes. It gives up
    // instead when less than 1 KB/s arrives for 30 seconds.
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_TIME, 30L);
    // Stops at an error status, so the error page isn't written to file
    curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_to_file);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &download);
    // The progress callback is off unless NOPROGRESS is
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, on_transfer_info);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &download);

    CURLcode res = curl_easy_perform(curl.get());
    if (download.error) std::rethrow_exception(download.error);
    if (res == CURLE_ABORTED_BY_CALLBACK) throw DownloadCancelled();
    if (res == CURLE_WRITE_ERROR) throw std::runtime_error(std::format("Couldn't write {}", file.string()));
    check_response(curl.get(), res, url);

    // Writes what's still buffered, which can fail too, e.g. on a full disk
    download.out.close();
    if (!download.out) throw std::runtime_error(std::format("Couldn't write {}", file.string()));
}

std::string url_encode(std::string_view text){
    CurlHandle curl = make_handle();
    char* encoded = curl_easy_escape(curl.get(), text.data(), static_cast<int>(text.size()));
    if (!encoded) throw std::runtime_error("curl_easy_escape failed");

    std::string result(encoded);
    curl_free(encoded);
    return result;
}
