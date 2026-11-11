#include "core/zip_archive.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <stdexcept>

// zlib
#include <zlib.h>

namespace fs = std::filesystem;

// The numbers in a zip are little-endian, whatever the computer is
static std::uint16_t u16(const char* at){
    const auto* b = reinterpret_cast<const unsigned char*>(at);
    return static_cast<std::uint16_t>(b[0] | b[1] << 8);
}

static std::uint32_t u32(const char* at){
    const auto* b = reinterpret_cast<const unsigned char*>(at);
    return static_cast<std::uint32_t>(b[0]) | static_cast<std::uint32_t>(b[1]) << 8
         | static_cast<std::uint32_t>(b[2]) << 16 | static_cast<std::uint32_t>(b[3]) << 24;
}

// What each part of an archive starts with
static constexpr std::uint32_t END_SIGNATURE = 0x06054b50;      // The end record, after the list
static constexpr std::uint32_t LISTED_SIGNATURE = 0x02014b50;   // A file in the list
static constexpr std::uint32_t FILE_SIGNATURE = 0x04034b50;     // A file's own header, before its data
static constexpr size_t END_SIZE = 22, LISTED_SIZE = 46, FILE_SIZE = 30;
// How a file is stored
static constexpr std::uint16_t STORED = 0, DEFLATED = 8;

static std::runtime_error damaged(const fs::path& file){
    return std::runtime_error(std::format("{} isn't a zip archive, or is damaged", file.filename().string()));
}

// size bytes of in from offset
static std::string read_at(std::ifstream& in, std::uint64_t offset, std::uint64_t size, const fs::path& file){
    std::string bytes(size, '\0');
    in.seekg(static_cast<std::streamoff>(offset));
    in.read(bytes.data(), static_cast<std::streamsize>(size));
    if (!in || static_cast<std::uint64_t>(in.gcount()) != size) throw damaged(file);
    return bytes;
}

ZipArchive::ZipArchive(fs::path path) : file(std::move(path)){
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error(std::format("Couldn't open {}", file.string()));
    in.seekg(0, std::ios::end);
    const std::uint64_t size = static_cast<std::uint64_t>(in.tellg());
    if (size < END_SIZE) throw damaged(file);

    // The end record is last, but for a comment of up to 64 KiB after it
    const std::uint64_t tail_size = std::min<std::uint64_t>(size, END_SIZE + 0xFFFF);
    const std::string tail = read_at(in, size - tail_size, tail_size, file);
    size_t end = std::string::npos;
    for (size_t at = tail.size() - END_SIZE + 1; at-- > 0;){
        if (u32(tail.data() + at) == END_SIGNATURE){
            end = at;
            break;
        }
    }
    if (end == std::string::npos) throw damaged(file);

    const std::uint16_t count = u16(tail.data() + end + 10);
    const std::uint32_t list_size = u32(tail.data() + end + 12);
    const std::uint32_t list_offset = u32(tail.data() + end + 16);
    // All ones means the real number is in a Zip64 record
    if (count == 0xFFFF || list_size == 0xFFFFFFFF || list_offset == 0xFFFFFFFF)
        throw std::runtime_error(std::format("{} is too big an archive to read", file.filename().string()));
    if (static_cast<std::uint64_t>(list_offset) + list_size > size) throw damaged(file);

    const std::string list = read_at(in, list_offset, list_size, file);
    size_t at = 0;
    for (std::uint16_t i = 0; i < count; ++i){
        if (at + LISTED_SIZE > list.size() || u32(list.data() + at) != LISTED_SIGNATURE) throw damaged(file);
        const char* listed = list.data() + at;
        const size_t name_size = u16(listed + 28), extra_size = u16(listed + 30), comment_size = u16(listed + 32);
        if (at + LISTED_SIZE + name_size + extra_size + comment_size > list.size()) throw damaged(file);

        Entry entry;
        entry.name.assign(listed + LISTED_SIZE, name_size);
        entry.encrypted = u16(listed + 8) & 1;
        entry.method = u16(listed + 10);
        entry.crc = u32(listed + 16);
        entry.stored_size = u32(listed + 20);
        entry.size = u32(listed + 24);
        entry.offset = u32(listed + 42);
        entries_.push_back(std::move(entry));
        at += LISTED_SIZE + name_size + extra_size + comment_size;
    }
}

const ZipArchive::Entry* ZipArchive::find(std::string_view name) const {
    const auto found = std::ranges::find(entries_, name, &Entry::name);
    return found == entries_.end() ? nullptr : &*found;
}

std::string ZipArchive::read(const Entry& entry) const {
    const std::runtime_error bad(std::format("{} in {} is damaged", entry.name, file.filename().string()));
    if (entry.encrypted) throw std::runtime_error(std::format("{} is encrypted", file.filename().string()));
    if (entry.method != STORED && entry.method != DEFLATED)
        throw std::runtime_error(std::format("{} is compressed in a way that can't be read", file.filename().string()));

    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error(std::format("Couldn't open {}", file.string()));
    // The file's own header has its name again, and extra fields that can
    // differ from the list's, so only it says where the data starts
    const std::string header = read_at(in, entry.offset, FILE_SIZE, file);
    if (u32(header.data()) != FILE_SIGNATURE) throw bad;
    const std::uint64_t data = entry.offset + FILE_SIZE + u16(header.data() + 26) + u16(header.data() + 28);
    std::string stored = read_at(in, data, entry.stored_size, file);

    std::string contents;
    if (entry.method == STORED){
        contents = std::move(stored);
    } else {
        contents.resize(entry.size);
        z_stream stream{};
        // -15: deflate's own format, with no zlib header, as zips have it
        if (inflateInit2(&stream, -15) != Z_OK) throw std::runtime_error("Couldn't start reading the archive");
        stream.next_in = reinterpret_cast<Bytef*>(stored.data());
        stream.avail_in = static_cast<uInt>(stored.size());
        stream.next_out = reinterpret_cast<Bytef*>(contents.data());
        stream.avail_out = static_cast<uInt>(contents.size());
        const int result = inflate(&stream, Z_FINISH);
        const bool whole = result == Z_STREAM_END && stream.total_out == entry.size;
        inflateEnd(&stream);
        if (!whole) throw bad;
    }

    if (contents.size() != entry.size) throw bad;
    const uLong crc = crc32(0, reinterpret_cast<const Bytef*>(contents.data()), static_cast<uInt>(contents.size()));
    if (crc != entry.crc) throw bad;
    return contents;
}
