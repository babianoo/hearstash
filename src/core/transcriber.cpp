#include "core/transcriber.h"

#include <format>
#include <fstream>
#include <stdexcept>

// whisper.cpp
#include <whisper.h>

static_assert(Transcriber::SAMPLE_RATE == WHISPER_SAMPLE_RATE);

// whisper.cpp reads the model through these, from the ifstream in
// context. Its own whisper_init_from_file_with_params opens the path as
// UTF-8 only when built with MSVC; with MinGW a non-ASCII character in
// it, e.g. in the user name, would fail. std::ifstream takes the
// std::filesystem::path as it is.
static size_t read_model(void* context, void* output, size_t size){
    auto* in = static_cast<std::ifstream*>(context);
    in->read(static_cast<char*>(output), static_cast<std::streamsize>(size));
    return static_cast<size_t>(in->gcount());
}

static bool model_eof(void* context){
    return static_cast<std::ifstream*>(context)->eof();
}

// The ifstream closes itself when load() returns
static void close_model(void*){}

void Transcriber::ContextDeleter::operator()(whisper_context* ctx) const {
    whisper_free(ctx);
}

Transcriber::Transcriber() = default;
Transcriber::~Transcriber() = default;

void Transcriber::unload(){
    ctx.reset();
    model = nullptr;
}

void Transcriber::load(const WhisperModel& new_model){
    // Freed first, so two models are never in memory at once
    unload();

    if (!is_installed(new_model))
        throw std::runtime_error(std::format("The {} model isn't downloaded", new_model.id));

    std::ifstream in(model_path(new_model), std::ios::binary);
    if (!in) throw std::runtime_error(std::format("Couldn't open the {} model", new_model.id));

    whisper_model_loader loader{&in, read_model, model_eof, close_model};
    ctx.reset(whisper_init_with_params(&loader, whisper_context_default_params()));
    // It was checked against its SHA-1 when downloaded, so it's most
    // likely run out of memory, or the file was changed since
    if (!ctx) throw std::runtime_error(std::format("Couldn't load the {} model", new_model.id));
    model = &new_model;
}

std::string Transcriber::transcribe(const WhisperModel& wanted, std::span<const float> samples,
                                    const std::string& language){
    if (model != &wanted) load(wanted);
    if (samples.empty()) return "";

    // Greedy: the best token each step. Beam search is a little more
    // accurate on long audio and several times slower.
    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.language = language.c_str();
    // Its own console output, which the app doesn't want
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.print_special = false;   // e.g. [BLANK_AUDIO]
    // Each recording is separate: text from the last one would only
    // mislead it
    params.no_context = true;
    // Only the text is used
    params.no_timestamps = true;
    params.suppress_blank = true;

    if (whisper_full(ctx.get(), params, samples.data(), static_cast<int>(samples.size())) != 0)
        throw std::runtime_error("whisper.cpp couldn't transcribe the audio");

    std::string text;
    const int segments = whisper_full_n_segments(ctx.get());
    for (int i = 0; i < segments; ++i) text += whisper_full_get_segment_text(ctx.get(), i);

    // Segments start with a space
    text.erase(text.find_last_not_of(" \t\r\n") + 1);
    text.erase(0, text.find_first_not_of(" \t\r\n"));
    return text;
}
