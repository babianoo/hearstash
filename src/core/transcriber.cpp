#include "core/transcriber.h"

#include <cstdio>
#include <format>
#include <fstream>
#include <mutex>
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

// whisper.cpp and ggml log through this instead of straight to stderr.
// Only warnings and errors get through: loading a model logs about a
// hundred lines of sizes and settings at INFO.
static void log_warnings(ggml_log_level level, const char* text, void*){
    // A CONT message carries on the one before, so it goes where that went.
    // Per thread, since whichever thread is transcribing logs.
    thread_local bool shown = false;
    if (level != GGML_LOG_LEVEL_CONT) shown = level >= GGML_LOG_LEVEL_WARN;
    if (shown) std::fputs(text, stderr);
}

// Called while whisper computes, often enough to stop in about half a
// second. Returning true aborts it.
static bool stop_requested(void* data){
    return static_cast<std::stop_token*>(data)->stop_requested();
}

void Transcriber::ContextDeleter::operator()(whisper_context* ctx) const {
    whisper_free(ctx);
}

Transcriber::Transcriber(){
    // For the whole program, so once is enough
    static std::once_flag quiet;
    std::call_once(quiet, []{ whisper_log_set(log_warnings, nullptr); });
}

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
                                    const std::string& language, std::stop_token stop){
    if (stop.stop_requested()) throw TranscriptionCancelled();
    if (model != &wanted) load(wanted);
    // Loading has no way to stop partway, so it's checked again after
    if (stop.stop_requested()) throw TranscriptionCancelled();
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
    // Tokens that aren't speech, like (音楽) ("music"), which it otherwise
    // gives for a recording of room noise
    params.suppress_nst = true;
    params.abort_callback = stop_requested;
    params.abort_callback_user_data = &stop;

    if (whisper_full(ctx.get(), params, samples.data(), static_cast<int>(samples.size())) != 0){
        // An abort fails it like an error does, and logs "failed to encode"
        if (stop.stop_requested()) throw TranscriptionCancelled();
        throw std::runtime_error("whisper.cpp couldn't transcribe the audio");
    }
    // It may have finished just as the stop came: the text is kept anyway,
    // since it's done

    std::string text;
    const int segments = whisper_full_n_segments(ctx.get());
    for (int i = 0; i < segments; ++i) text += whisper_full_get_segment_text(ctx.get(), i);

    // Segments start with a space
    text.erase(text.find_last_not_of(" \t\r\n") + 1);
    text.erase(0, text.find_first_not_of(" \t\r\n"));
    return text;
}
