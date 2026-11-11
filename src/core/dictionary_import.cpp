#include "core/dictionary_import.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <format>
#include <stdexcept>
#include <system_error>
#include <vector>

// JSON
#include <nlohmann/json.hpp>

#include "core/http.h"
#include "core/verified_download.h"
#include "core/zip_archive.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

// The N of "term_bank_N.json", or -1 if name isn't one
static int term_bank_number(std::string_view name){
    static constexpr std::string_view PREFIX = "term_bank_", SUFFIX = ".json";
    if (!name.starts_with(PREFIX) || !name.ends_with(SUFFIX)) return -1;
    name.remove_prefix(PREFIX.size());
    name.remove_suffix(SUFFIX.size());
    int number = -1;
    const auto [end, error] = std::from_chars(name.data(), name.data() + name.size(), number);
    return error == std::errc() && end == name.data() + name.size() && number >= 0 ? number : -1;
}

// j as text, or empty if it's something else, e.g. the null a dictionary
// may give for no tags
static std::string text_of(const json& j){
    return j.is_string() ? j.get<std::string>() : "";
}

static DictionaryInfo read_info(const ZipArchive& zip, const fs::path& zip_file){
    const std::runtime_error not_one(std::format("{} isn't a dictionary in Yomitan's format", zip_file.filename().string()));
    const ZipArchive::Entry* index = zip.find("index.json");
    if (!index) throw not_one;

    DictionaryInfo info;
    try {
        const json j = json::parse(zip.read(*index));
        // "version" is the older name for it
        const int format = j.value("format", j.value("version", 0));
        if (format != 3)
            throw std::runtime_error(std::format("{} is in format {} of Yomitan's dictionaries; only 3 can be read",
                                                 zip_file.filename().string(), format));
        info.title = j.at("title").get<std::string>();
        info.revision = j.at("revision").get<std::string>();
        info.author = j.value("author", "");
        info.url = j.value("url", "");
        info.attribution = j.value("attribution", "");
        // Only given by a dictionary that can be updated
        info.index_url = j.value("indexUrl", "");
        info.download_url = j.value("downloadUrl", "");
    } catch (const json::exception&){
        throw not_one;
    }
    if (const ZipArchive::Entry* styles = zip.find("styles.css")) info.styles = zip.read(*styles);
    info.converted = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return info;
}

// Adds a term bank's rows
static void add_bank(DictionaryWriter& writer, const std::string& bank, std::string_view name){
    const std::runtime_error bad(std::format("{} in the dictionary isn't as the format says", name));
    try {
        const json rows = json::parse(bank);
        if (!rows.is_array()) throw bad;
        for (const json& row : rows){
            // term, reading, tags, rules, score, definitions, sequence,
            // and tags for the term, which nothing here uses
            if (!row.is_array() || row.size() < 7 || !row[0].is_string() || !row[5].is_array()) throw bad;

            // A definition that's a list, ["悪どい", [...]], says the
            // word is listed under that spelling rather than what it means
            std::string redirect;
            for (const json& definition : row[5])
                if (definition.is_array() && !definition.empty() && definition[0].is_string())
                    redirect = definition[0].get<std::string>();

            writer.add_row(row[0].get<std::string>(), text_of(row[1]), text_of(row[2]), text_of(row[3]),
                           row[4].is_number() ? static_cast<int>(row[4].get<double>()) : 0,
                           row[6].is_number() ? static_cast<int>(row[6].get<double>()) : 0,
                           redirect.empty() ? row[5].dump() : std::string(), redirect);
        }
    } catch (const json::exception&){
        throw bad;
    }
}

DictionaryInfo import_dictionary(const fs::path& zip_file, const fs::path& file, const ImportProgress& on_progress,
                                 std::stop_token stop){
    const ZipArchive zip(zip_file);
    DictionaryInfo info = read_info(zip, zip_file);

    // In the order the dictionary numbers them, which is the order its
    // rows are meant to be in
    std::vector<std::pair<int, const ZipArchive::Entry*>> banks;
    for (const ZipArchive::Entry& entry : zip.entries())
        if (const int number = term_bank_number(entry.name); number >= 0) banks.emplace_back(number, &entry);
    std::ranges::sort(banks, {}, &std::pair<int, const ZipArchive::Entry*>::first);
    if (banks.empty())
        throw std::runtime_error(std::format("{} has no words in it", zip_file.filename().string()));

    if (file.has_parent_path()) fs::create_directories(file.parent_path());
    const fs::path part = partial_path(file);
    try {
        // In a block of its own so the file is closed before it's renamed
        {
            DictionaryWriter writer(part);
            for (size_t i = 0; i < banks.size(); ++i){
                if (stop.stop_requested()) throw DownloadCancelled();
                if (on_progress) on_progress(i, banks.size());
                add_bank(writer, zip.read(*banks[i].second), banks[i].second->name);
            }
            if (stop.stop_requested()) throw DownloadCancelled();
            writer.finish(info);
        }
        if (on_progress) on_progress(banks.size(), banks.size());
    } catch (...){
        std::error_code ec;
        fs::remove(part, ec);
        throw;
    }
    fs::rename(part, file);
    return info;
}
