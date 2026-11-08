#include "core/text_reader.h"

#include <stdexcept>

#ifdef HAVE_ONNXRUNTIME

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

#include <onnxruntime_cxx_api.h>

#include "core/ocr_text.h"

// What the detection model's pixels are normalized with: ImageNet's mean
// and spread, from its inference.yml. PaddleOCR lists them for RGB but
// applies them to the image as it decodes it, BGR, so the first is
// blue's.
static constexpr std::array DETECTION_MEAN{0.485f, 0.456f, 0.406f};
static constexpr std::array DETECTION_STD{0.229f, 0.224f, 0.225f};

struct TextReader::Models {
    // Has to outlive the sessions
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "hearstash"};
    Ort::Session detection{nullptr};
    Ort::Session recognition{nullptr};
    std::vector<std::string> characters;
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
};

static std::string read_file(const std::filesystem::path& file){
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error(std::format("Couldn't open {}", file.string()));
    return {std::istreambuf_iterator<char>(in), {}};
}

// Runs a model with one input and one output, both float tensors, and
// returns the output and its shape
static std::pair<std::vector<float>, std::vector<int64_t>> run(Ort::Session& session, const Ort::MemoryInfo& memory,
                                                               std::vector<float>& input,
                                                               const std::array<int64_t, 4>& shape){
    Ort::AllocatorWithDefaultOptions allocator;
    const Ort::AllocatedStringPtr input_name = session.GetInputNameAllocated(0, allocator);
    const Ort::AllocatedStringPtr output_name = session.GetOutputNameAllocated(0, allocator);
    const char* input_names[] = {input_name.get()};
    const char* output_names[] = {output_name.get()};

    Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), shape.data(), shape.size());
    std::vector<Ort::Value> outputs = session.Run(Ort::RunOptions{nullptr}, input_names, &tensor, 1, output_names, 1);
    const Ort::TensorTypeAndShapeInfo info = outputs.front().GetTensorTypeAndShapeInfo();
    const float* data = outputs.front().GetTensorData<float>();
    return {std::vector<float>(data, data + info.GetElementCount()), info.GetShape()};
}

// image as the planes of a model's input, blue first, each value made
// (v / 255 - mean) / std
static void to_planes(const Image& image, std::span<float> out, int out_w, std::span<const float, 3> mean,
                      std::span<const float, 3> std){
    const size_t plane = out.size() / 3;
    const int out_h = static_cast<int>(plane / static_cast<size_t>(out_w));
    for (int y = 0; y < image.height && y < out_h; ++y)
        for (int x = 0; x < image.width && x < out_w; ++x){
            const std::uint8_t* px = &image.rgb[(static_cast<size_t>(y) * image.width + x) * 3];
            const size_t at = static_cast<size_t>(y) * out_w + x;
            for (int c = 0; c < 3; ++c){
                // BGR: blue is the first plane
                const float v = px[2 - c] / 255.0f;
                out[static_cast<size_t>(c) * plane + at] = (v - mean[static_cast<size_t>(c)]) / std[static_cast<size_t>(c)];
            }
        }
}

TextReader::TextReader(const std::filesystem::path& detection_model, const std::filesystem::path& recognition_model,
                       const std::filesystem::path& recognition_settings)
    : models(std::make_unique<Models>()){
    try {
        models->characters = parse_character_list(read_file(recognition_settings));
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        // Paths are wide on Windows, as onnxruntime takes them there
        models->detection = Ort::Session(models->env, detection_model.c_str(), options);
        models->recognition = Ort::Session(models->env, recognition_model.c_str(), options);
    } catch (const Ort::Exception& e){
        throw std::runtime_error(std::format("Couldn't load the text reading models: {}", e.what()));
    }
}

TextReader::~TextReader() = default;

std::string TextReader::read(const Image& image, std::stop_token stop){
    if (image.empty()) return "";
    try {
        // --- Finding the lines ---
        // Black rows above and below a very wide or short picture, so the
        // text isn't squashed flat when it's scaled
        const int pad = detection_padding(image.width, image.height);
        Image canvas = image;
        if (pad > 0){
            canvas.height = image.height + 2 * pad;
            canvas.rgb.assign(static_cast<size_t>(canvas.width) * canvas.height * 3, 0);
            std::ranges::copy(image.rgb, canvas.rgb.begin() + static_cast<std::ptrdiff_t>(pad) * canvas.width * 3);
        }
        const auto [det_w, det_h] = detection_size(canvas.width, canvas.height);
        std::vector<float> det_input(static_cast<size_t>(det_w) * det_h * 3);
        to_planes(resize(canvas, det_w, det_h), det_input, det_w, DETECTION_MEAN, DETECTION_STD);
        const auto [map, map_shape] = run(models->detection, models->memory, det_input, {1, 3, det_h, det_w});
        if (map_shape.size() != 4 || map_shape[2] != det_h || map_shape[3] != det_w)
            throw std::runtime_error("The text finding model gave an unexpected output");

        std::vector<Rect> boxes;
        for (Rect box : boxes_from_map(map, det_w, det_h, canvas.width, canvas.height)){
            // Back to the picture without the padding
            const int top = std::max(box.y - pad, 0), bottom = std::min(box.y + box.h - pad, image.height);
            if (bottom - top <= 3) continue;
            boxes.push_back({.x = box.x, .y = top, .w = box.w, .h = bottom - top});
        }
        if (boxes.empty()) return "";

        // --- Reading them ---
        constexpr std::array<float, 3> HALF{0.5f, 0.5f, 0.5f};
        std::vector<std::string> lines;
        const std::vector<TextLine> rows = layout_lines(boxes);
        for (const TextLine& row : rows){
            std::string line;
            for (size_t i : row.boxes){
                if (stop.stop_requested()) return join_lines(lines);
                Image part = crop(image, boxes[i]);
                if (part.empty()) continue;
                if (is_vertical(boxes[i])) part = rotate_left(part);

                const auto [scaled_w, padded_w] = recognition_width(part.width, part.height);
                // Zero after normalizing: the padding is mid-grey
                std::vector<float> input(static_cast<size_t>(padded_w) * RECOGNITION_HEIGHT * 3, 0.0f);
                to_planes(resize(part, scaled_w, RECOGNITION_HEIGHT), input, padded_w, HALF, HALF);
                const auto [scores, shape] = run(models->recognition, models->memory, input,
                                                 {1, 3, RECOGNITION_HEIGHT, padded_w});
                if (shape.size() != 3 || shape[2] != static_cast<int64_t>(models->characters.size()))
                    throw std::runtime_error("The text reading model doesn't match its list of characters");
                line += decode_line(scores, static_cast<int>(shape[1]), static_cast<int>(shape[2]), models->characters);
            }
            lines.push_back(tidy_line(line, lines.size() + 1 == rows.size()));
        }
        return join_lines(lines);
    } catch (const Ort::Exception& e){
        throw std::runtime_error(std::format("Couldn't read the text: {}", e.what()));
    }
}

#else

struct TextReader::Models {};

TextReader::TextReader(const std::filesystem::path&, const std::filesystem::path&, const std::filesystem::path&){
    throw std::runtime_error("This copy of Hearstash was built without onnxruntime, so it can't read text on the screen.");
}

TextReader::~TextReader() = default;

std::string TextReader::read(const Image&, std::stop_token){
    return "";
}

#endif
