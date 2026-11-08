// Reads the text in saved screenshots with TextReader and prints it, to
// check the OCR without the app:
//
//   build/read_text <models folder> <screenshot.png>...
//
// The models folder has the files the app downloads (see
// core/ocr_models.h): text-detection.onnx, text-recognition.onnx and
// text-recognition.yml. Prints one JSON object per screenshot:
// {"file": ..., "ms": ..., "text": ...}.

#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/screenshot.h"
#include "core/text_reader.h"

int main(int argc, char** argv){
    if (argc < 3){
        std::cerr << "Usage: read_text <models folder> <screenshot.png>...\n";
        return 2;
    }
    try {
        const std::filesystem::path models = argv[1];
        TextReader reader(models / "text-detection.onnx", models / "text-recognition.onnx",
                          models / "text-recognition.yml");
        for (int i = 2; i < argc; ++i){
            std::ifstream in(argv[i], std::ios::binary);
            const std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(in), {}};
            const Image image = parse_png(data);

            const auto start = std::chrono::steady_clock::now();
            const std::string text = reader.read(image);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            std::cout << nlohmann::json{{"file", std::filesystem::path(argv[i]).filename().string()},
                                        {"ms", ms.count()}, {"text", text}}.dump() << '\n';
        }
    } catch (const std::exception& e){
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
