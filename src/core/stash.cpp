#include "core/stash.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <system_error>

// JSON
#include <nlohmann/json.hpp>

#include "core/atomic_write.h"
#include "core/local_time.h"
#include "core/media_encoding.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

// Written into stash.json. Bump it when a field is renamed or changes
// meaning.
static constexpr int VERSION = 1;
// The screenshot is encoded again for a card, at the card's quality, so
// it's kept close to how it was
static constexpr int SCREENSHOT_QUALITY = 95;
static constexpr int THUMBNAIL_QUALITY = 80;

static constexpr const char* AUDIO_FILE = "audio.wav";
static constexpr const char* SCREENSHOT_FILE = "screenshot.jpg";
static constexpr const char* THUMBNAIL_FILE = "thumbnail.jpg";
static constexpr const char* INFO_FILE = "stash.json";

static std::string_view as_text(const std::vector<std::uint8_t>& bytes){
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

static std::vector<std::uint8_t> read_file(const fs::path& file){
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error(std::format("Couldn't open {}", file.string()));
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) throw std::runtime_error(std::format("Couldn't read {}", file.string()));
    return bytes;
}

// A folder name that's only a name, not a path elsewhere, e.g. "../x"
static bool plain_name(std::string_view id){
    return !id.empty() && id != "." && id != ".." && id.find_first_of("/\\:") == std::string_view::npos;
}

// What the folder's files take, skipping any that went away meanwhile
static std::uintmax_t folder_bytes(const fs::path& folder){
    std::uintmax_t total = 0;
    std::error_code error;
    for (const fs::directory_entry& file : fs::directory_iterator(folder, error)){
        const std::uintmax_t size = file.file_size(error);
        if (!error) total += size;
    }
    return total;
}

StashEntry stash_line(const fs::path& dir, const StashCapture& capture){
    if (capture.audio.empty() || capture.rate <= 0) throw std::runtime_error("There's nothing to stash yet");

    fs::create_directories(dir);
#ifndef _WIN32
    // Recordings of what the user watched: theirs alone, like the files
    fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace);
#endif

    // A second stash in the same second gets "-2", and so on
    const std::string base = format_time(local_time(capture.time), "%Y%m%d-%H%M%S");
    std::string id = base;
    for (int n = 2; fs::exists(dir / id); ++n) id = std::format("{}-{}", base, n);
    const fs::path folder = dir / id;
    fs::create_directory(folder);

    StashEntry entry{.id = id, .time = capture.time, .app = capture.app, .rate = capture.rate,
                     .selection_seconds = capture.selection_seconds,
                     .screenshot_error = capture.screenshot_error, .transcript = {}, .transcript_error = {},
                     .seconds = static_cast<double>(capture.audio.size()) / capture.rate, .bytes = 0};
    try {
        write_file_atomically(folder / AUDIO_FILE, as_text(encode_wav(capture.audio, capture.rate)));
        if (capture.screenshot && !capture.screenshot->empty()){
            write_file_atomically(folder / SCREENSHOT_FILE, as_text(encode_jpeg(*capture.screenshot, SCREENSHOT_QUALITY)));
            const Image thumbnail = shrink_to_height(*capture.screenshot, STASH_THUMBNAIL_HEIGHT);
            write_file_atomically(folder / THUMBNAIL_FILE, as_text(encode_jpeg(thumbnail, THUMBNAIL_QUALITY)));
        } else if (entry.screenshot_error.empty()){
            entry.screenshot_error = "No screenshot was taken";
        }
        const json info = {{"time", entry.time}, {"app", entry.app}, {"rate", entry.rate},
                           {"selection_seconds", entry.selection_seconds},
                           {"screenshot_error", entry.screenshot_error}, {"transcript", ""},
                           {"transcript_error", ""}, {"version", VERSION}};
        write_file_atomically(folder / INFO_FILE, info.dump(2));
    } catch (...){
        std::error_code ignored;
        fs::remove_all(folder, ignored);
        throw;
    }
    entry.bytes = folder_bytes(folder);
    return entry;
}

// Throws for a stash.json that's missing, broken or from a newer version
static StashEntry read_entry(const fs::path& folder){
    std::ifstream in(folder / INFO_FILE);
    if (!in) throw std::runtime_error("no stash.json");
    const json info = json::parse(in);
    if (info.value("version", 0) > VERSION) throw std::runtime_error("saved by a newer version");

    StashEntry entry;
    entry.id = folder.filename().string();
    info.at("time").get_to(entry.time);
    info.at("rate").get_to(entry.rate);
    info.at("selection_seconds").get_to(entry.selection_seconds);
    entry.app = info.value("app", "");
    entry.screenshot_error = info.value("screenshot_error", "");
    entry.transcript = info.value("transcript", "");
    entry.transcript_error = info.value("transcript_error", "");
    if (entry.rate <= 0) throw std::runtime_error("no sample rate");

    // The WAV header is 44 bytes, then 2 bytes a sample
    const std::uintmax_t audio_bytes = fs::file_size(folder / AUDIO_FILE);
    entry.seconds = audio_bytes > 44 ? static_cast<double>(audio_bytes - 44) / 2 / entry.rate : 0;
    entry.bytes = folder_bytes(folder);
    return entry;
}

std::vector<StashEntry> list_stash(const fs::path& dir){
    std::vector<StashEntry> entries;
    std::error_code error;
    for (const fs::directory_entry& item : fs::directory_iterator(dir, error)){
        if (!item.is_directory(error)) continue;
        // Being written right now, or cut off by a crash: Clear removes it
        if (!fs::exists(item.path() / INFO_FILE, error)) continue;
        try {
            entries.push_back(read_entry(item.path()));
        } catch (const std::exception& e){
            std::cerr << std::format("Skipping the stashed line {}: {}\n", item.path().string(), e.what());
        }
    }
    // Newest first; the name breaks ties, so "-2" comes before the first
    std::ranges::sort(entries, [](const StashEntry& a, const StashEntry& b){
        return a.time != b.time ? a.time > b.time : a.id > b.id;
    });
    return entries;
}

StashedLine load_stashed(const fs::path& dir, const StashEntry& entry){
    if (!plain_name(entry.id)) throw std::runtime_error("That isn't a stashed line");
    const fs::path folder = dir / entry.id;

    StashedLine line;
    try {
        DecodedWav wav = decode_wav(read_file(folder / AUDIO_FILE));
        line.audio = std::move(wav.samples);
        line.rate = wav.rate;
    } catch (const std::exception& e){
        throw std::runtime_error(std::format("Couldn't read the stashed audio: {}", e.what()));
    }
    line.selection_seconds = entry.selection_seconds;
    line.screenshot_error = entry.screenshot_error;
    line.transcript = entry.transcript;
    if (line.screenshot_error.empty()){
        try {
            line.screenshot = decode_jpeg(read_file(folder / SCREENSHOT_FILE));
        } catch (const std::exception& e){
            line.screenshot_error = e.what();
        }
    }
    return line;
}

void set_stash_transcript(const fs::path& dir, std::string_view id, std::string_view transcript,
                          std::string_view error){
    if (!plain_name(id)) throw std::runtime_error("That isn't a stashed line");
    const fs::path file = dir / id / INFO_FILE;
    // Not written if it's gone: write_file_atomically would make the
    // folder again, with nothing else in it
    std::ifstream in(file);
    if (!in) throw std::runtime_error("The stashed line is gone");
    json info = json::parse(in);
    in.close();
    info["transcript"] = transcript;
    info["transcript_error"] = error;
    write_file_atomically(file, info.dump(2));
}

Image load_stash_thumbnail(const fs::path& dir, const StashEntry& entry){
    if (!plain_name(entry.id) || !entry.screenshot_error.empty()) return {};
    try {
        return decode_jpeg(read_file(dir / entry.id / THUMBNAIL_FILE));
    } catch (const std::exception&){
        return {};
    }
}

void delete_stashed(const fs::path& dir, std::string_view id){
    // Never anything outside the stash
    if (!plain_name(id)) return;
    fs::remove_all(dir / id);
}

void clear_stash(const fs::path& dir){
    std::error_code error;
    for (const fs::directory_entry& item : fs::directory_iterator(dir, error))
        fs::remove_all(item.path());
}
