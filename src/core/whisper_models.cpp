#include "core/whisper_models.h"

#include <format>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <vector>

#include "core/config_dir.h"
#include "core/sha1.h"

namespace fs = std::filesystem;

// The same files whisper.cpp's download-ggml-model scripts fetch
static constexpr const char* DOWNLOAD_BASE = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main";

// e.g. ggml-small.bin, the name whisper.cpp's scripts give it too
static std::string file_name(const WhisperModel& model){
    return std::format("ggml-{}.bin", model.id);
}

const WhisperModel* find_model(std::string_view id){
    for (const WhisperModel& model : WHISPER_MODELS)
        if (model.id == id) return &model;
    return nullptr;
}

fs::path models_dir(){
    return data_dir() / "models";
}

fs::path model_path(const WhisperModel& model){
    return models_dir() / file_name(model);
}

fs::path partial_path(const WhisperModel& model){
    fs::path part = model_path(model);
    part += ".part";
    return part;
}

std::string model_url(const WhisperModel& model){
    return std::format("{}/{}", DOWNLOAD_BASE, file_name(model));
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

bool is_installed(const WhisperModel& model){
    // A folder with the model's name isn't a model
    std::error_code ec;
    return fs::is_regular_file(model_path(model), ec);
}

void download_model(const WhisperModel& model, const DownloadProgress& on_progress,
                    std::stop_token stop){
    fs::create_directories(models_dir());

    fs::path part = partial_path(model);
    try {
        http_download(model_url(model), part, on_progress, stop);
        check_sha1(part, model.sha1, stop);
    } catch (...){
        // Starting over is simpler than resuming, and a cancelled large
        // model shouldn't keep a gigabyte of disk
        std::error_code ec;
        fs::remove(part, ec);
        throw;
    }
    // Only now does it have the model's own name, so a file with that
    // name is always whole and undamaged
    fs::rename(part, model_path(model));
}

void remove_model(const WhisperModel& model){
    // Returns false rather than throwing when the file isn't there
    fs::remove(model_path(model));
    fs::remove(partial_path(model));
}
