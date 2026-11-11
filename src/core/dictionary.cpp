#include "core/dictionary.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <stdexcept>

// JSON
#include <nlohmann/json.hpp>
// zlib
#include <zlib.h>

#include "core/sha1.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

// ---- The file ----
//
//   "HSDICT\0\0"                     8 bytes
//   u32 version, u32 section count
//   MAX_SECTIONS times:              32 bytes each, unused ones all 0
//     u32 id, u32 packed (1 if the section is deflated with zlib)
//     u64 offset in the file, u64 size in the file, u64 size unpacked
//   the blocks of definitions, each deflated
//   the sections
//
// Every number is little-endian. The sections, by id:
//   META      JSON: a DictionaryInfo's fields
//   KEYS      the keys' bytes, sorted bytewise, with nothing between them
//   KEY_ENDS  u32 per key: where it ends in KEYS (it starts where the one
//             before ends)
//   KEY_ROWS  u32 per key, and one more: where the key's rows start in
//             ROW_LIST (they end where the next key's start)
//   ROW_LIST  u32 each: a row's index
//   ROWS      per row, ROW_SIZE bytes:
//               u32 term, u32 reading   indexes of keys; NONE for no reading
//               u32 tags, u32 rules, u32 redirect   offsets into TEXT
//               i32 score, i32 sequence
//               u32 block, u32 offset, u32 length   its definitions: which
//                 block, and where in it once unpacked. length 0 for none.
//   TEXT      strings, each ending in a 0 byte. Offset 0 is the empty one.
//   BLOCKS    per block: u64 offset in the file, u32 size in the file,
//             u32 size unpacked

static constexpr char MAGIC[8] = {'H', 'S', 'D', 'I', 'C', 'T', 0, 0};
static constexpr std::uint32_t VERSION = 1;
static constexpr size_t MAX_SECTIONS = 8;
static constexpr size_t HEADER_SIZE = sizeof(MAGIC) + 8 + MAX_SECTIONS * 32;
enum Section : std::uint32_t { META = 1, KEYS, KEY_ENDS, KEY_ROWS, ROW_LIST, ROWS, TEXT, BLOCKS };
static constexpr std::uint32_t NONE = 0xFFFFFFFF;
static constexpr size_t BLOCK_ENTRY_SIZE = 16;
// How much text a block of definitions holds before it's compressed.
// Bigger packs tighter, but a lookup unpacks a whole block for one entry:
// at this size that takes a fraction of a millisecond.
static constexpr size_t BLOCK_SIZE = 64 * 1024;

static void put_u32(std::string& to, std::uint32_t n){
    for (int shift = 0; shift < 32; shift += 8) to += static_cast<char>(n >> shift & 0xFF);
}

static void put_u64(std::string& to, std::uint64_t n){
    for (int shift = 0; shift < 64; shift += 8) to += static_cast<char>(n >> shift & 0xFF);
}

static std::uint32_t get_u32(std::string_view from, size_t at){
    std::uint32_t n = 0;
    for (int i = 0; i < 4; ++i) n |= static_cast<std::uint32_t>(static_cast<unsigned char>(from[at + i])) << (8 * i);
    return n;
}

static std::uint64_t get_u64(std::string_view from, size_t at){
    return get_u32(from, at) | static_cast<std::uint64_t>(get_u32(from, at + 4)) << 32;
}

static std::string deflated(std::string_view raw){
    uLongf size = compressBound(static_cast<uLong>(raw.size()));
    std::string packed(size, '\0');
    if (compress2(reinterpret_cast<Bytef*>(packed.data()), &size, reinterpret_cast<const Bytef*>(raw.data()),
                  static_cast<uLong>(raw.size()), Z_BEST_COMPRESSION) != Z_OK)
        throw std::runtime_error("Couldn't compress the dictionary");
    packed.resize(size);
    return packed;
}

// Throws std::runtime_error if packed doesn't unpack to raw_size bytes
static std::string inflated(std::string_view packed, std::uint64_t raw_size){
    std::string raw(raw_size, '\0');
    uLongf size = static_cast<uLongf>(raw_size);
    const int result = uncompress(reinterpret_cast<Bytef*>(raw.data()), &size,
                                  reinterpret_cast<const Bytef*>(packed.data()), static_cast<uLong>(packed.size()));
    if (result != Z_OK || size != raw_size) throw std::runtime_error("The dictionary file is damaged");
    return raw;
}

// ---- Reading ----

// size bytes of in from offset. Throws if they aren't all there.
static std::string read_at(std::ifstream& in, std::uint64_t offset, std::uint64_t size){
    std::string bytes(size, '\0');
    in.seekg(static_cast<std::streamoff>(offset));
    in.read(bytes.data(), static_cast<std::streamsize>(size));
    if (!in || static_cast<std::uint64_t>(in.gcount()) != size) throw std::runtime_error("The dictionary file is damaged");
    return bytes;
}

Dictionary::Dictionary(fs::path path) : file(std::move(path)){
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error(std::format("Couldn't open {}", file.string()));
    std::string header(HEADER_SIZE, '\0');
    in.read(header.data(), static_cast<std::streamsize>(header.size()));
    if (static_cast<size_t>(in.gcount()) != header.size() || std::memcmp(header.data(), MAGIC, sizeof(MAGIC)) != 0)
        throw std::runtime_error(std::format("{} isn't a dictionary file", file.filename().string()));
    if (get_u32(header, 8) != VERSION)
        throw std::runtime_error(std::format("{} is a dictionary file for another version of the app",
                                             file.filename().string()));

    in.seekg(0, std::ios::end);
    const std::uint64_t file_size = static_cast<std::uint64_t>(in.tellg());
    std::string meta;
    const std::uint32_t count = std::min<std::uint32_t>(get_u32(header, 12), MAX_SECTIONS);
    for (std::uint32_t i = 0; i < count; ++i){
        const size_t at = sizeof(MAGIC) + 8 + i * 32;
        const std::uint32_t id = get_u32(header, at);
        const bool packed = get_u32(header, at + 4) == 1;
        const std::uint64_t offset = get_u64(header, at + 8), stored = get_u64(header, at + 16), raw = get_u64(header, at + 24);
        std::string* section = id == META ? &meta : id == KEYS ? &keys : id == KEY_ENDS ? &key_ends
                             : id == KEY_ROWS ? &key_rows : id == ROW_LIST ? &row_list : id == ROWS ? &rows
                             : id == TEXT ? &text : id == BLOCKS ? &blocks : nullptr;
        // One this version doesn't know of
        if (!section) continue;
        // Before anything that size is allocated
        if (offset > file_size || stored > file_size - offset || raw > (1ull << 31))
            throw std::runtime_error("The dictionary file is damaged");
        *section = read_at(in, offset, stored);
        if (packed) *section = inflated(*section, raw);
    }

    // So that reading them later can't run off their ends
    const bool whole = key_ends.size() % 4 == 0 && key_rows.size() == key_ends.size() + 4
                    && row_list.size() % 4 == 0 && rows.size() % ROW_SIZE == 0
                    && blocks.size() % BLOCK_ENTRY_SIZE == 0 && !text.empty() && text.back() == '\0'
                    && (key_count() == 0 || get_u32(key_ends, key_ends.size() - 4) == keys.size())
                    && get_u32(key_rows, key_rows.size() - 4) == row_list.size() / 4;
    if (!whole) throw std::runtime_error("The dictionary file is damaged");

    try {
        const json j = json::parse(meta);
        info_.title = j.value("title", "");
        info_.revision = j.value("revision", "");
        info_.author = j.value("author", "");
        info_.url = j.value("url", "");
        info_.attribution = j.value("attribution", "");
        info_.index_url = j.value("index_url", "");
        info_.download_url = j.value("download_url", "");
        info_.styles = j.value("styles", "");
        info_.converted = j.value("converted", std::int64_t{0});
    } catch (const json::exception&){
        throw std::runtime_error("The dictionary file is damaged");
    }
}

std::string_view Dictionary::key(size_t i) const {
    const std::uint32_t start = i == 0 ? 0 : get_u32(key_ends, (i - 1) * 4);
    const std::uint32_t end = get_u32(key_ends, i * 4);
    if (start > end || end > keys.size()) return {};
    return std::string_view(keys).substr(start, end - start);
}

std::string_view Dictionary::text_at(std::uint32_t offset) const {
    if (offset >= text.size()) return {};
    // Up to the 0 after it; the constructor checked the last byte is one
    return std::string_view(text.data() + offset);
}

DictionaryRow Dictionary::row(std::uint32_t index) const {
    const size_t at = static_cast<size_t>(index) * ROW_SIZE;
    DictionaryRow row;
    row.index = index;
    const std::uint32_t term = get_u32(rows, at), reading = get_u32(rows, at + 4);
    if (term < key_count()) row.term = key(term);
    if (reading != NONE && reading < key_count()) row.reading = key(reading);
    row.tags = text_at(get_u32(rows, at + 8));
    row.rules = text_at(get_u32(rows, at + 12));
    row.redirect = text_at(get_u32(rows, at + 16));
    row.score = static_cast<std::int32_t>(get_u32(rows, at + 20));
    row.sequence = static_cast<std::int32_t>(get_u32(rows, at + 24));
    return row;
}

std::vector<DictionaryRow> Dictionary::find(std::string_view wanted) const {
    // The keys are sorted, so each look halves where it can be
    size_t low = 0, high = key_count();
    while (low < high){
        const size_t middle = low + (high - low) / 2;
        if (key(middle) < wanted) low = middle + 1;
        else high = middle;
    }
    if (low == key_count() || key(low) != wanted) return {};

    std::vector<DictionaryRow> found;
    const std::uint32_t first = get_u32(key_rows, low * 4), last = get_u32(key_rows, (low + 1) * 4);
    for (std::uint32_t i = first; i < last && i < row_list.size() / 4; ++i){
        const std::uint32_t index = get_u32(row_list, static_cast<size_t>(i) * 4);
        if (index < row_count()) found.push_back(row(index));
    }
    return found;
}

json Dictionary::definitions(const DictionaryRow& row) const {
    if (row.index >= row_count()) return json::array();
    const size_t at = static_cast<size_t>(row.index) * ROW_SIZE;
    const std::uint32_t block = get_u32(rows, at + 28), offset = get_u32(rows, at + 32), length = get_u32(rows, at + 36);
    if (length == 0) return json::array();
    if (block >= blocks.size() / BLOCK_ENTRY_SIZE) throw std::runtime_error("The dictionary file is damaged");

    const size_t entry = static_cast<size_t>(block) * BLOCK_ENTRY_SIZE;
    const std::uint32_t raw_size = get_u32(blocks, entry + 12);
    if (static_cast<std::uint64_t>(offset) + length > raw_size) throw std::runtime_error("The dictionary file is damaged");

    // Opened for this read only, so the file can be replaced by a newer
    // one while the app runs
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error(std::format("Couldn't open {}", file.string()));
    const std::string text = inflated(read_at(in, get_u64(blocks, entry), get_u32(blocks, entry + 8)), raw_size);
    try {
        return json::parse(std::string_view(text).substr(offset, length));
    } catch (const json::exception&){
        throw std::runtime_error("The dictionary file is damaged");
    }
}

// ---- Writing ----

DictionaryWriter::DictionaryWriter(const fs::path& path) : file(path), out(path, std::ios::binary | std::ios::trunc){
    if (!out) throw std::runtime_error(std::format("Couldn't write {}", file.string()));
    // The header's place, filled in by finish()
    write(std::string(HEADER_SIZE, '\0'));
}

void DictionaryWriter::write(std::string_view bytes){
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error(std::format("Couldn't write {}", file.string()));
}

std::uint32_t DictionaryWriter::intern(std::string_view s){
    if (s.empty()) return 0;
    const auto [found, added] = text_offsets.try_emplace(std::string(s), static_cast<std::uint32_t>(text.size()));
    if (added){
        text += s;
        text += '\0';
    }
    return found->second;
}

void DictionaryWriter::end_block(){
    if (block.empty()) return;
    const std::string packed = deflated(block);
    put_u64(blocks, static_cast<std::uint64_t>(out.tellp()));
    put_u32(blocks, static_cast<std::uint32_t>(packed.size()));
    put_u32(blocks, static_cast<std::uint32_t>(block.size()));
    write(packed);
    block.clear();
    ++block_count;
}

void DictionaryWriter::add_row(std::string_view term, std::string_view reading, std::string_view tags,
                               std::string_view rules, int score, int sequence, std::string_view definitions,
                               std::string_view redirect){
    Row row{.term = std::string(term), .reading = std::string(reading), .tags = intern(tags), .rules = intern(rules),
            .redirect = intern(redirect), .score = score, .sequence = sequence, .block = 0, .offset = 0, .length = 0};

    if (redirect.empty() && !definitions.empty()){
        // The same definitions as an earlier row's are that row's
        Sha1 sha1;
        sha1.update(definitions.data(), definitions.size());
        const auto [found, added] = written.try_emplace(sha1.hex_digest());
        if (added){
            found->second = {block_count, static_cast<std::uint32_t>(block.size()),
                             static_cast<std::uint32_t>(definitions.size())};
            block += definitions;
        }
        row.block = found->second.block;
        row.offset = found->second.offset;
        row.length = found->second.length;
        // After the row has its place: the block ending doesn't move it
        if (block.size() >= BLOCK_SIZE) end_block();
    }
    rows.push_back(std::move(row));
}

void DictionaryWriter::finish(const DictionaryInfo& info){
    end_block();

    // Every term and reading, once, sorted
    std::vector<std::string_view> sorted;
    sorted.reserve(rows.size() * 2);
    for (const Row& row : rows){
        sorted.push_back(row.term);
        if (!row.reading.empty()) sorted.push_back(row.reading);
    }
    std::ranges::sort(sorted);
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    const auto index_of = [&](std::string_view key){
        return static_cast<std::uint32_t>(std::ranges::lower_bound(sorted, key) - sorted.begin());
    };

    std::string keys, key_ends;
    for (std::string_view key : sorted){
        keys += key;
        put_u32(key_ends, static_cast<std::uint32_t>(keys.size()));
    }

    // Each key's rows: counted, then given their places, then put there
    std::vector<std::uint32_t> term_keys(rows.size()), reading_keys(rows.size(), NONE);
    std::vector<std::uint32_t> starts(sorted.size() + 1, 0);
    for (size_t r = 0; r < rows.size(); ++r){
        term_keys[r] = index_of(rows[r].term);
        ++starts[term_keys[r] + 1];
        if (!rows[r].reading.empty() && rows[r].reading != rows[r].term){
            reading_keys[r] = index_of(rows[r].reading);
            ++starts[reading_keys[r] + 1];
        }
    }
    for (size_t k = 0; k < sorted.size(); ++k) starts[k + 1] += starts[k];
    std::vector<std::uint32_t> list(starts.back());
    std::vector<std::uint32_t> next(starts.begin(), starts.end() - 1);
    for (size_t r = 0; r < rows.size(); ++r){
        list[next[term_keys[r]]++] = static_cast<std::uint32_t>(r);
        if (reading_keys[r] != NONE) list[next[reading_keys[r]]++] = static_cast<std::uint32_t>(r);
    }

    std::string key_rows, row_list, row_bytes;
    for (std::uint32_t start : starts) put_u32(key_rows, start);
    for (std::uint32_t r : list) put_u32(row_list, r);
    row_bytes.reserve(rows.size() * Dictionary::ROW_SIZE);
    for (size_t r = 0; r < rows.size(); ++r){
        const Row& row = rows[r];
        put_u32(row_bytes, term_keys[r]);
        // A reading that's the word itself is still said to be the reading
        put_u32(row_bytes, row.reading.empty() ? NONE : row.reading == row.term ? term_keys[r] : reading_keys[r]);
        put_u32(row_bytes, row.tags);
        put_u32(row_bytes, row.rules);
        put_u32(row_bytes, row.redirect);
        put_u32(row_bytes, static_cast<std::uint32_t>(row.score));
        put_u32(row_bytes, static_cast<std::uint32_t>(row.sequence));
        put_u32(row_bytes, row.block);
        put_u32(row_bytes, row.offset);
        put_u32(row_bytes, row.length);
    }

    const json meta = {
        {"title", info.title}, {"revision", info.revision}, {"author", info.author}, {"url", info.url},
        {"attribution", info.attribution}, {"index_url", info.index_url}, {"download_url", info.download_url},
        {"styles", info.styles}, {"converted", info.converted},
    };

    // The lists compress well: the keys are sorted, the rows alike
    std::string header(MAGIC, sizeof(MAGIC));
    put_u32(header, VERSION);
    const std::pair<Section, const std::string*> sections[] = {
        {KEYS, &keys}, {KEY_ENDS, &key_ends}, {KEY_ROWS, &key_rows}, {ROW_LIST, &row_list},
        {ROWS, &row_bytes}, {TEXT, &text}, {BLOCKS, &blocks},
    };
    put_u32(header, static_cast<std::uint32_t>(std::size(sections) + 1));
    const auto add_section = [&](Section id, const std::string& raw){
        const std::string packed = deflated(raw);
        put_u32(header, id);
        put_u32(header, 1);
        put_u64(header, static_cast<std::uint64_t>(out.tellp()));
        put_u64(header, packed.size());
        put_u64(header, raw.size());
        write(packed);
    };
    add_section(META, meta.dump());
    for (const auto& [id, raw] : sections) add_section(id, *raw);
    header.resize(HEADER_SIZE, '\0');

    out.seekp(0);
    write(header);
    out.close();
    if (out.fail()) throw std::runtime_error(std::format("Couldn't write {}", file.string()));
}
