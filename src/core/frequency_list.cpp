#include "core/frequency_list.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <system_error>

// JSON
#include <nlohmann/json.hpp>
// zlib
#include <zlib.h>

#include "core/config_dir.h"
#include "core/verified_download.h"
#include "core/zip_archive.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

static constexpr char MAGIC[8] = {'H', 'S', 'F', 'R', 'E', 'Q', 0, 0};
static constexpr std::uint32_t VERSION = 1;
// More than any real list unpacks to, so a damaged size can't make it
// allocate gigabytes
static constexpr std::uint64_t MAX_ROWS_SIZE = 1ull << 30;

static void put_u32(std::string& to, std::uint32_t n){
    for (int shift = 0; shift < 32; shift += 8) to += static_cast<char>(n >> shift & 0xFF);
}

static void put_u64(std::string& to, std::uint64_t n){
    for (int shift = 0; shift < 64; shift += 8) to += static_cast<char>(n >> shift & 0xFF);
}

static std::uint64_t get_le(std::string_view from, size_t at, int bytes){
    std::uint64_t n = 0;
    for (int i = 0; i < bytes; ++i) n |= static_cast<std::uint64_t>(static_cast<unsigned char>(from[at + i])) << (8 * i);
    return n;
}

// ---- Reading ----

FrequencyList::FrequencyList(const fs::path& file){
    const std::runtime_error damaged("The frequency file is damaged");
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error(std::format("Couldn't open {}", file.string()));
    const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (bytes.size() < 16 || std::memcmp(bytes.data(), MAGIC, sizeof(MAGIC)) != 0)
        throw std::runtime_error(std::format("{} isn't a frequency file", file.filename().string()));
    if (get_le(bytes, 8, 4) != VERSION)
        throw std::runtime_error(std::format("{} is a frequency file for another version of the app",
                                             file.filename().string()));

    const std::uint64_t meta_size = get_le(bytes, 12, 4);
    if (meta_size > bytes.size() - 16 || bytes.size() - 16 - meta_size < 8) throw damaged;
    const std::string_view meta = std::string_view(bytes).substr(16, meta_size);
    const size_t packed_at = 16 + meta_size + 8;
    const std::uint64_t raw_size = get_le(bytes, 16 + meta_size, 8);
    if (raw_size > MAX_ROWS_SIZE) throw damaged;

    try {
        const json j = json::parse(meta);
        info_.title = j.value("title", "");
        info_.revision = j.value("revision", "");
        info_.author = j.value("author", "");
        info_.url = j.value("url", "");
        info_.attribution = j.value("attribution", "");
        info_.description = j.value("description", "");
        info_.converted = j.value("converted", std::int64_t{0});
    } catch (const json::exception&){
        throw damaged;
    }

    rows.assign(raw_size, '\0');
    uLongf size = static_cast<uLongf>(raw_size);
    if (uncompress(reinterpret_cast<Bytef*>(rows.data()), &size, reinterpret_cast<const Bytef*>(bytes.data() + packed_at),
                   static_cast<uLong>(bytes.size() - packed_at)) != Z_OK || size != raw_size)
        throw damaged;
    if (rows.size() > UINT32_MAX || (!rows.empty() && rows.back() != '\n')) throw damaged;

    for (size_t at = 0; at < rows.size(); at = rows.find('\n', at) + 1){
        // term, reading and what's shown: two tabs before the line ends
        const size_t end = rows.find('\n', at), first = rows.find('\t', at);
        if (first >= end || rows.find('\t', first + 1) >= end) throw damaged;
        starts.push_back(static_cast<std::uint32_t>(at));
    }
}

std::string_view FrequencyList::term_at(size_t i) const {
    const size_t at = starts[i];
    return std::string_view(rows).substr(at, rows.find('\t', at) - at);
}

std::vector<std::string> FrequencyList::find(std::string_view term, std::string_view reading) const {
    if (reading.empty()) reading = term;
    // Sorted by term, so the term's rows are together
    size_t low = 0, high = starts.size();
    while (low < high){
        const size_t middle = low + (high - low) / 2;
        if (term_at(middle) < term) low = middle + 1;
        else high = middle;
    }

    std::vector<std::string> shown;
    for (size_t i = low; i < starts.size() && term_at(i) == term; ++i){
        const size_t reading_at = starts[i] + term.size() + 1;
        const size_t shown_at = rows.find('\t', reading_at) + 1;
        const std::string_view row_reading = std::string_view(rows).substr(reading_at, shown_at - 1 - reading_at);
        if (!row_reading.empty() && row_reading != reading) continue;
        shown.emplace_back(std::string_view(rows).substr(shown_at, rows.find('\n', shown_at) - shown_at));
    }
    return shown;
}

// ---- Making one ----

// The N of "term_meta_bank_N.json", or -1 if name isn't one
static int meta_bank_number(std::string_view name){
    static constexpr std::string_view PREFIX = "term_meta_bank_", SUFFIX = ".json";
    if (!name.starts_with(PREFIX) || !name.ends_with(SUFFIX)) return -1;
    name.remove_prefix(PREFIX.size());
    name.remove_suffix(SUFFIX.size());
    int number = -1;
    const auto [end, error] = std::from_chars(name.data(), name.data() + name.size(), number);
    return error == std::errc() && end == name.data() + name.size() && number >= 0 ? number : -1;
}

// text with nothing in it that would end a field or row of the file
static std::string one_field(std::string text){
    std::ranges::replace(text, '\t', ' ');
    std::ranges::replace(text, '\n', ' ');
    std::ranges::replace(text, '\r', ' ');
    return text;
}

// A frequency as Yomitan shows it: its displayValue if it has one, or else
// its value. A number, a string or {"value": ..., "displayValue": ...}.
// Empty if it's none of those.
static std::string shown_of(const json& frequency){
    if (frequency.is_string()) return frequency.get<std::string>();
    if (frequency.is_number()){
        const double value = frequency.get<double>();
        if (std::isfinite(value) && value == std::floor(value) && std::abs(value) < 1e15)
            return std::to_string(static_cast<long long>(value));
        return std::format("{}", value);
    }
    if (frequency.is_object()){
        if (const auto shown = frequency.find("displayValue"); shown != frequency.end() && shown->is_string())
            return shown->get<std::string>();
        if (const auto value = frequency.find("value"); value != frequency.end()) return shown_of(*value);
    }
    return "";
}

namespace {
struct ImportedRow {
    std::string term, reading, shown;
};
}

// Adds a term meta bank's "freq" rows
static void add_bank(std::vector<ImportedRow>& rows, const std::string& bank, std::string_view name){
    const std::runtime_error bad(std::format("{} in the frequency dictionary isn't as the format says", name));
    try {
        const json list = json::parse(bank);
        if (!list.is_array()) throw bad;
        for (const json& row : list){
            if (!row.is_array() || row.size() < 3 || !row[0].is_string() || !row[1].is_string()) throw bad;
            // Pitch accents and readings are in these banks too
            if (row[1] != "freq") continue;
            const json& data = row[2];
            std::string reading, shown;
            if (data.is_object() && data.contains("frequency")){
                if (const auto r = data.find("reading"); r != data.end() && r->is_string()) reading = r->get<std::string>();
                shown = shown_of(data["frequency"]);
            } else {
                shown = shown_of(data);
            }
            if (shown.empty()) continue;
            rows.push_back({one_field(row[0].get<std::string>()), one_field(std::move(reading)), one_field(std::move(shown))});
        }
    } catch (const json::exception&){
        throw bad;
    }
}

FrequencyInfo import_frequencies(const fs::path& zip_file, const fs::path& file, std::stop_token stop){
    const ZipArchive zip(zip_file);
    const std::runtime_error not_one(std::format("{} isn't a frequency dictionary in Yomitan's format",
                                                 zip_file.filename().string()));
    const ZipArchive::Entry* index = zip.find("index.json");
    if (!index) throw not_one;

    FrequencyInfo info;
    try {
        const json j = json::parse(zip.read(*index));
        const int format = j.value("format", j.value("version", 0));
        if (format != 3)
            throw std::runtime_error(std::format("{} is in format {} of Yomitan's dictionaries; only 3 can be read",
                                                 zip_file.filename().string(), format));
        info.title = j.at("title").get<std::string>();
        info.revision = j.value("revision", "");
        info.author = j.value("author", "");
        info.url = j.value("url", "");
        info.attribution = j.value("attribution", "");
        info.description = j.value("description", "");
    } catch (const json::exception&){
        throw not_one;
    }
    info.converted = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::vector<std::pair<int, const ZipArchive::Entry*>> banks;
    for (const ZipArchive::Entry& entry : zip.entries())
        if (const int number = meta_bank_number(entry.name); number >= 0) banks.emplace_back(number, &entry);
    std::ranges::sort(banks, {}, &std::pair<int, const ZipArchive::Entry*>::first);

    std::vector<ImportedRow> imported;
    for (const auto& [number, entry] : banks){
        if (stop.stop_requested()) throw DownloadCancelled();
        add_bank(imported, zip.read(*entry), entry->name);
    }
    if (imported.empty())
        throw std::runtime_error(std::format("{} has no frequencies in it", zip_file.filename().string()));
    // A term's rows together, and still in the order the dictionary has
    // them, which is the order they're shown in
    std::ranges::stable_sort(imported, {}, &ImportedRow::term);

    std::string raw;
    for (const ImportedRow& row : imported) raw += row.term + '\t' + row.reading + '\t' + row.shown + '\n';
    if (raw.size() > UINT32_MAX) throw std::runtime_error("The frequency dictionary is too big");
    if (stop.stop_requested()) throw DownloadCancelled();

    uLongf packed_size = compressBound(static_cast<uLong>(raw.size()));
    std::string packed(packed_size, '\0');
    if (compress2(reinterpret_cast<Bytef*>(packed.data()), &packed_size, reinterpret_cast<const Bytef*>(raw.data()),
                  static_cast<uLong>(raw.size()), Z_BEST_COMPRESSION) != Z_OK)
        throw std::runtime_error("Couldn't compress the frequencies");
    packed.resize(packed_size);

    const std::string meta = json{
        {"title", info.title}, {"revision", info.revision}, {"author", info.author}, {"url", info.url},
        {"attribution", info.attribution}, {"description", info.description}, {"converted", info.converted},
    }.dump();
    std::string bytes(MAGIC, sizeof(MAGIC));
    put_u32(bytes, VERSION);
    put_u32(bytes, static_cast<std::uint32_t>(meta.size()));
    bytes += meta;
    put_u64(bytes, raw.size());
    bytes += packed;

    if (file.has_parent_path()) fs::create_directories(file.parent_path());
    const fs::path part = partial_path(file);
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (out.fail()){
            std::error_code ec;
            fs::remove(part, ec);
            throw std::runtime_error(std::format("Couldn't write {}", file.string()));
        }
    }
    fs::rename(part, file);
    return info;
}

// ---- The app's frequency list ----

fs::path frequency_path(){
    return data_dir() / "dictionary" / FREQUENCY_FILE_NAME;
}

static std::mutex current_mutex;
static std::shared_ptr<const FrequencyList> current;
static bool current_loaded = false;

std::shared_ptr<const FrequencyList> current_frequencies(){
    const std::lock_guard lock(current_mutex);
    if (!current_loaded){
        current_loaded = true;
        try {
            const fs::path file = frequency_path();
            std::error_code error;
            if (fs::exists(file, error)) current = std::make_shared<const FrequencyList>(file);
        } catch (const std::exception&){
            // Damaged or from another version: as good as not there
            current.reset();
        }
    }
    return current;
}

void reload_frequencies(){
    const std::lock_guard lock(current_mutex);
    current.reset();
    current_loaded = false;
}

FrequencyInfo download_frequencies(const DownloadProgress& on_download, std::stop_token stop){
    const fs::path file = frequency_path();
    const fs::path zip = file.parent_path() / "frequencies.zip";
    try {
        download_verified(FREQUENCY_URL, zip, FREQUENCY_SHA1, on_download, stop);
        FrequencyInfo info = import_frequencies(zip, file, stop);
        std::error_code error;
        fs::remove(zip, error);
        reload_frequencies();
        return info;
    } catch (...){
        std::error_code error;
        fs::remove(zip, error);
        throw;
    }
}

void delete_frequencies(){
    fs::remove(frequency_path());
    reload_frequencies();
}
