#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string_view>

#include "core/http.h"

// The models that read text on the screen (OCR): PaddleOCR's PP-OCRv6
// small, as PaddlePaddle publishes them on Hugging Face as ONNX (Apache
// 2.0), at a fixed commit. About 30 MB, kept in models_dir()/ocr/ next to
// the whisper models, and downloaded the first time they're needed.

struct OcrFile {
    // The file's name in models_dir()/ocr/
    std::string_view name;
    std::string_view url;
    std::string_view sha1;
    std::uint64_t size;
};

inline constexpr std::array OCR_FILES{
    // Finds the lines of text
    OcrFile{"text-detection.onnx",
            "https://huggingface.co/PaddlePaddle/PP-OCRv6_small_det_onnx/resolve/"
            "28fe5895c24fd108c19eb3e8479f4ab385fbfc62/inference.onnx",
            "0f6f376e35c01e153d821b5a7ace65c033ebcec0", 9880512},
    // Reads each line
    OcrFile{"text-recognition.onnx",
            "https://huggingface.co/PaddlePaddle/PP-OCRv6_small_rec_onnx/resolve/"
            "b8f84f0b80c529de40b4fbb3544b84fa7233a513/inference.onnx",
            "9b57c2a96f3bab8898567a7885fe319a1643fdba", 21159378},
    // The reading model's settings, with the characters it reads
    OcrFile{"text-recognition.yml",
            "https://huggingface.co/PaddlePaddle/PP-OCRv6_small_rec_onnx/resolve/"
            "b8f84f0b80c529de40b4fbb3544b84fa7233a513/inference.yml",
            "f9a20da38958c2019287e5cab55d167ea381167a", 150579},
};

// The path functions throw std::runtime_error if data_dir() can't be
// found. None of them create anything.
std::filesystem::path ocr_models_dir();
std::filesystem::path ocr_file_path(const OcrFile& file);

// Whether all of OCR_FILES are there. False as well if the folder can't
// be read.
bool ocr_models_installed();

// Downloads the files that aren't there yet, each checked against its
// SHA-1 (see download_verified). Blocks for seconds to minutes, so not on
// the UI thread. on_progress gets the bytes received and the total of
// all the files. Throws what download_verified throws.
void download_ocr_models(const DownloadProgress& on_progress = {}, std::stop_token stop = {});
