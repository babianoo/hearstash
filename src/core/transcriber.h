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
// loaded the first time it's needed and kept, since loading takes seconds
// and hundreds of MB; a call with another model swaps it.
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
    // it isn't the one loaded.
    // Requesting stop on stop makes it throw TranscriptionCancelled, about
    // half a second later while transcribing (with base). Loading can't be
    // stopped partway, so then it throws once the model is loaded.
    // Throws std::runtime_error if model isn't installed, can't be loaded,
    // or whisper fails.
    std::string transcribe(const WhisperModel& model, std::span<const float> samples,
                           const std::string& language = "ja", std::stop_token stop = {});

    // nullptr if none is
    const WhisperModel* loaded_model() const { return model; }
    // Frees the model's memory; the next transcribe() loads it again
    void unload();

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

    // Replaces what's loaded with model
    void load(const WhisperModel& model);
};
