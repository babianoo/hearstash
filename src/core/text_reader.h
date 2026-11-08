#pragma once

#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>

#include "core/image.h"

// Reads the Japanese text in a picture, e.g. a screenshot of a visual
// novel, with PaddleOCR's PP-OCRv6 small models run by onnxruntime: one
// finds the lines, the other reads each. About 150 ms for a 1920x1080
// screen on an 8-core laptop, and less for a crop.
//
// Horizontal text is read top to bottom, vertical text right to left,
// and a screen can have both (see layout_lines()). Furigana is left out.
// See core/ocr_text.h for the fix-ups done to each line.
//
// Built without onnxruntime (no HAVE_ONNXRUNTIME), the constructor
// throws, saying so.
class TextReader {
public:
    // Loads the models, which takes a fraction of a second: the detection
    // model, the recognition model, and the recognition model's
    // inference.yml, which lists the characters it reads. Throws
    // std::runtime_error if they can't be loaded.
    TextReader(const std::filesystem::path& detection_model, const std::filesystem::path& recognition_model,
               const std::filesystem::path& recognition_settings);
    // Defined in text_reader.cpp, where Models is complete
    ~TextReader();

    TextReader(const TextReader&) = delete;
    TextReader& operator=(const TextReader&) = delete;

    // The text in image, its lines joined with join_lines(). Empty if it
    // has none. Requesting stop on stop makes it return what it has read
    // so far at the next line. Throws std::runtime_error if a model fails.
    // One thread at a time.
    std::string read(const Image& image, std::stop_token stop = {});

private:
    // onnxruntime's sessions, kept out of this header so only
    // text_reader.cpp needs onnxruntime's
    struct Models;
    std::unique_ptr<Models> models;
};
