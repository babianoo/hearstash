#include "core/dictionary_files.h"

#include <charconv>
#include <exception>
#include <format>
#include <stdexcept>
#include <system_error>

// JSON
#include <nlohmann/json.hpp>

#include "core/config_dir.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

fs::path shipped_dictionary_path(){
    const fs::path program = program_dir();
    const fs::path beside = program / DICTIONARY_FILE_NAME;
    const fs::path shared = program.parent_path() / "share" / "hearstash" / DICTIONARY_FILE_NAME;
    std::error_code error;
    if (!fs::exists(beside, error) && fs::exists(shared, error)) return shared;
    return beside;
}

fs::path updated_dictionary_path(){
    return data_dir() / "dictionary" / DICTIONARY_FILE_NAME;
}

// The dictionary at the path get gives, or nullptr if there's none that
// can be read
template <typename GetPath>
static std::shared_ptr<const Dictionary> open_at(const GetPath& get){
    try {
        const fs::path file = get();
        std::error_code error;
        if (!fs::exists(file, error)) return nullptr;
        return std::make_shared<const Dictionary>(file);
    } catch (const std::exception&){
        // Damaged, from another version of the app, or its folder unknown:
        // as good as not there
        return nullptr;
    }
}

std::shared_ptr<const Dictionary> open_installed_dictionary(){
    auto shipped = open_at(shipped_dictionary_path);
    auto updated = open_at(updated_dictionary_path);
    if (!shipped) return updated;
    if (!updated) return shipped;
    // A newer app can ship a newer one than an old update fetched
    return updated->info().revision >= shipped->info().revision ? updated : shipped;
}

int dictionary_age_days(const DictionaryInfo& info, std::chrono::system_clock::time_point now){
    using namespace std::chrono;
    sys_days released = floor<days>(sys_seconds(seconds(info.converted)));

    // "2026.10.03.0": year, month, day, and which of that day's
    int parts[3] = {};
    const char* at = info.revision.data();
    const char* const end = at + info.revision.size();
    bool dated = true;
    for (int& part : parts){
        const auto [after, error] = std::from_chars(at, end, part);
        if (error != std::errc() || (after != end && *after != '.')){
            dated = false;
            break;
        }
        at = after == end ? after : after + 1;
    }
    if (dated){
        const year_month_day date{year(parts[0]), month(static_cast<unsigned>(parts[1])), day(static_cast<unsigned>(parts[2]))};
        if (date.ok()) released = sys_days(date);
    }

    const auto age = floor<days>(now) - released;
    return age.count() < 0 ? 0 : static_cast<int>(age.count());
}

DictionaryRelease latest_dictionary_release(const DictionaryInfo* installed){
    const std::string url = installed && !installed->index_url.empty() ? installed->index_url : DICTIONARY_INDEX_URL;
    const std::string body = http_get(url);
    try {
        // The same as the index.json in the zip
        const json j = json::parse(body);
        DictionaryRelease release{.title = j.value("title", ""), .revision = j.at("revision").get<std::string>(),
                                  .download_url = j.at("downloadUrl").get<std::string>()};
        if (!release.download_url.starts_with("https://"))
            throw std::runtime_error("The dictionary's download address isn't a secure one");
        return release;
    } catch (const json::exception&){
        throw std::runtime_error("Couldn't tell which dictionary is the latest: unexpected answer from its site");
    }
}

bool is_newer(const DictionaryRelease& release, const DictionaryInfo* installed){
    return !installed || release.revision > installed->revision;
}

DictionaryInfo update_dictionary(const DictionaryRelease& release, const DownloadProgress& on_download,
                                 const ImportProgress& on_import, std::stop_token stop){
    const fs::path file = updated_dictionary_path();
    fs::create_directories(file.parent_path());
    const fs::path zip = file.parent_path() / "download.zip";
    try {
        http_download(release.download_url, zip, on_download, stop);
        // Checks the zip as it reads it: every file in one has a checksum
        DictionaryInfo info = import_dictionary(zip, file, on_import, stop);
        std::error_code error;
        fs::remove(zip, error);
        return info;
    } catch (...){
        std::error_code error;
        fs::remove(zip, error);
        throw;
    }
}
