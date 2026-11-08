#include "core/verified_download.h"

#include <format>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <vector>

#include "core/sha1.h"

namespace fs = std::filesystem;

fs::path partial_path(const fs::path& file){
    fs::path part = file;
    part += ".part";
    return part;
}

// Throws std::runtime_error if file's SHA-1 isn't expected: the download
// was damaged, or the file on the server has changed. A large model takes
// seconds to read, so stop is checked between chunks, throwing
// DownloadCancelled like a stop during the download itself.
static void check_sha1(const fs::path& file, std::string_view expected, std::stop_token stop){
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error(std::format("Couldn't read {}", file.string()));

    Sha1 sha1;
    std::vector<char> chunk(1 << 20);
    while (in){
        if (stop.stop_requested()) throw DownloadCancelled();
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        sha1.update(chunk.data(), static_cast<std::size_t>(in.gcount()));
    }
    // eof() ends the loop too, which is the one that isn't an error
    if (in.bad()) throw std::runtime_error(std::format("Couldn't read {}", file.string()));

    const std::string actual = sha1.hex_digest();
    if (actual != expected)
        throw std::runtime_error(std::format("The download is damaged: its SHA-1 is {}, not {}", actual, expected));
}

void download_verified(const std::string& url, const fs::path& file, std::string_view sha1,
                       const DownloadProgress& on_progress, std::stop_token stop){
    fs::create_directories(file.parent_path());

    const fs::path part = partial_path(file);
    try {
        http_download(url, part, on_progress, stop);
        check_sha1(part, sha1, stop);
    } catch (...){
        // Starting over is simpler than resuming, and a cancelled large
        // model shouldn't keep a gigabyte of disk
        std::error_code ec;
        fs::remove(part, ec);
        throw;
    }
    // Only now does it have its own name
    fs::rename(part, file);
}
