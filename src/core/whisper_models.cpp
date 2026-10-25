#include "core/whisper_models.h"

#include <format>
#include <system_error>

#include "core/config_dir.h"

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

bool is_installed(const WhisperModel& model){
    // A folder with the model's name isn't a model
    std::error_code ec;
    return fs::is_regular_file(model_path(model), ec);
}

void remove_model(const WhisperModel& model){
    // Returns false rather than throwing when the file isn't there
    fs::remove(model_path(model));
    fs::remove(partial_path(model));
}
