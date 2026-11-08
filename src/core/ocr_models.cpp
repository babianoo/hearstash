#include "core/ocr_models.h"

#include <exception>
#include <string>
#include <system_error>

#include "core/verified_download.h"
#include "core/whisper_models.h"

namespace fs = std::filesystem;

fs::path ocr_models_dir(){
    return models_dir() / "ocr";
}

fs::path ocr_file_path(const OcrFile& file){
    return ocr_models_dir() / file.name;
}

bool ocr_models_installed(){
    try {
        std::error_code ec;
        for (const OcrFile& file : OCR_FILES)
            if (!fs::is_regular_file(ocr_file_path(file), ec)) return false;
        return true;
    } catch (const std::exception&){
        // No data_dir()
        return false;
    }
}

void download_ocr_models(const DownloadProgress& on_progress, std::stop_token stop){
    std::uint64_t total = 0, done = 0;
    for (const OcrFile& file : OCR_FILES) total += file.size;

    std::error_code ec;
    for (const OcrFile& file : OCR_FILES){
        const fs::path path = ocr_file_path(file);
        if (!fs::is_regular_file(path, ec)){
            download_verified(std::string(file.url), path, file.sha1, [&](std::uint64_t received, std::uint64_t){
                if (on_progress) on_progress(done + received, total);
            }, stop);
        }
        done += file.size;
        if (on_progress) on_progress(done, total);
    }
}
