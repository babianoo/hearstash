#pragma once

#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>

#include "core/whisper_models.h"

// whisper.h is only included in transcriber.cpp, so what includes this
// doesn't get all of whisper.cpp's declarations
struct whisper_context;

// Thrown by Transcriber::transcribe when it's stopped, so a cancel isn't
// reported as a failure
class TranscriptionCancelled : public std::runtime_error {
public:
    TranscriptionCancelled() : std::runtime_error("Transcription cancelled") {}
};

// Turns recorded speech into text with a whisper model. The model is
// loaded the first time it's needed and kept until unload(), since loading
// takes seconds; a call with another model, or another choice of GPU,
// swaps it.
//
// With gpu, the model runs on the GPU when there's one (gpu_name()) and it
// has the free memory for it, about 5 times faster than on the CPU.
// Otherwise it runs on the CPU, until the model is loaded again.
//
// One thread at a time, and not the UI thread: loading and transcribing
// both block for seconds.
class Transcriber {
public:
    // What whisper expects: mono, 16 kHz, floats from -1 to 1
    static constexpr int SAMPLE_RATE = 16000;

    // The first one also keeps whisper.cpp's informational messages, about
    // a hundred lines per model loaded, out of stderr. Its warnings and
    // errors still show.
    Transcriber();
    // Frees the model
    ~Transcriber();

    // It owns the loaded model, which is too big to copy
    Transcriber(const Transcriber&) = delete;
    Transcriber& operator=(const Transcriber&) = delete;

    // The text spoken in samples, trimmed; empty if there's none. language
    // is a code like "ja", or "auto" to detect it. Loads model first if
    // it isn't the one loaded, or was loaded with another gpu.
    // Requesting stop on stop makes it throw TranscriptionCancelled, about
    // half a second later while transcribing (with base). Loading can't be
    // stopped partway, so then it throws once the model is loaded.
    // Throws std::runtime_error if model isn't installed, can't be loaded,
    // or whisper fails.
    std::string transcribe(const WhisperModel& model, bool gpu, std::span<const float> samples,
                           const std::string& language = "ja", std::stop_token stop = {});

    // Loads model unless it's the one loaded, with the same gpu, so
    // transcribe() can start right away. Can't be stopped partway. Throws
    // std::runtime_error like transcribe() does.
    void prepare(const WhisperModel& model, bool gpu);

    // nullptr if none is
    const WhisperModel* loaded_model() const { return model; }
    // Whether the loaded model runs on the GPU. False if none is loaded.
    bool on_gpu() const { return ctx && gpu_used; }
    // Frees the model's memory; the next transcribe() loads it again
    void unload();

    // The GPU a model would run on, as its driver names it, e.g. "NVIDIA
    // GeForce GTX 1050 Ti". Empty if there's none, or the app was built
    // without GPU support. Any thread; the first call takes a moment, to
    // find the GPUs.
    static std::string gpu_name();

private:
    // whisper_free instead of delete. Defined in transcriber.cpp, which
    // has whisper.h.
    struct ContextDeleter {
        void operator()(whisper_context* ctx) const;
    };

    std::unique_ptr<whisper_context, ContextDeleter> ctx;
    // Which model ctx holds. Points into WHISPER_MODELS, which lives for
    // the whole program.
    const WhisperModel* model = nullptr;
    // The gpu it was loaded with, and whether it got the GPU
    bool gpu_wanted = false;
    bool gpu_used = false;

    // Replaces what's loaded with model
    void load(const WhisperModel& model, bool gpu);
};
