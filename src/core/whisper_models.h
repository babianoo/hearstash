#pragma once

#include <array>
#include <filesystem>
#include <stop_token>
#include <string>
#include <string_view>

#include "core/http.h"

// The whisper.cpp models the settings window offers, and where they're
// kept: data_dir()/models/ggml-<id>.bin. A model is installed if its file
// is there. Downloads go to <file>.part and are only renamed to the file
// once complete, so a file with the model's own name is always whole.

struct WhisperModel {
    // In the file name and settings.json, e.g. "small". Changing one makes
    // a downloaded copy unknown and a saved choice invalid.
    std::string_view id;
    // What the settings window shows, e.g. "466 MiB"
    std::string_view size;
    // Of the whole file, from whisper.cpp's models/README.md, to check a
    // download against
    std::string_view sha1;
};

// Smallest and fastest first. All multilingual: the .en models only know
// English, so they're no use here.
inline constexpr std::array WHISPER_MODELS{
    WhisperModel{"tiny",                "75 MiB",  "bd577a113a864445d4c299885e0cb97d4ba92b5f"},
    WhisperModel{"base",                "142 MiB", "465707469ff3a37a2b9b8d8f89f2f99de7299dac"},
    WhisperModel{"small",               "466 MiB", "55356645c2b361a969dfd0ef2c5a50d530afd8d5"},
    WhisperModel{"medium",              "1.5 GiB", "fd9727b6e1217c2f614f9b698455c4ffd82463b4"},
    // Close to large-v3's accuracy at half the size, and much faster
    WhisperModel{"large-v3-turbo",      "1.5 GiB", "4af2b29d7ec73d781377bfd1758ca957a807e941"},
    // The same, quantized: a little less accurate, a third of the size
    WhisperModel{"large-v3-turbo-q5_0", "547 MiB", "e050f7970618a659205450ad97eb95a18d69c9ee"},
};

// The model with that id, or nullptr if there's none, e.g. for an id saved
// by a version of the app that offered other models
const WhisperModel* find_model(std::string_view id);

// The path functions throw std::runtime_error if data_dir() can't be
// found. None of them create anything.
std::filesystem::path models_dir();
std::filesystem::path model_path(const WhisperModel& model);
// Where a download is written until it's complete
std::filesystem::path partial_path(const WhisperModel& model);

// Where to download it from: whisper.cpp's repository on Hugging Face
std::string model_url(const WhisperModel& model);

// False as well if the folder can't be read
bool is_installed(const WhisperModel& model);

// Downloads the model into models_dir(), creating it if needed, replacing
// a copy that's already there. Blocks for minutes, so not on the UI
// thread. on_progress and stop are http_download's. If it throws, the
// .part file is deleted and an installed copy is left as it was.
// Throws what http_download throws, or std::filesystem::filesystem_error.
void download_model(const WhisperModel& model, const DownloadProgress& on_progress = {},
                    std::stop_token stop = {});

// Deletes the model, and what a download left of it. Fine if neither
// exists. Throws std::filesystem::filesystem_error if one can't be
// deleted, e.g. on Windows while another program has it open.
void remove_model(const WhisperModel& model);
