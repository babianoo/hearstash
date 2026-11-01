#pragma once

#include <condition_variable>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>

#include "core/transcriber.h"

// The app's one Transcriber, shared by everything that turns speech into
// text: the search bar's microphone and the capture window. One model in
// memory, up to a few GB, instead of one each.
//
// Their threads take turns: one waits while another transcribes. The model
// stays loaded while anything holds it, and is freed UNLOAD_AFTER seconds
// after the last one lets go.
class SharedTranscriber {
public:
    // Seconds without transcribing before the model's memory is freed.
    // Loading it again takes seconds, but it starts as soon as it's
    // likely to be needed (a recording or a capture starts), so it's
    // mostly done by the time it is.
    static constexpr double UNLOAD_AFTER = 5 * 60;

    SharedTranscriber() = default;
    // Stops the timeout that frees the model
    ~SharedTranscriber();

    // The timeout holds a pointer to it. There's one per app.
    SharedTranscriber(const SharedTranscriber&) = delete;
    SharedTranscriber& operator=(const SharedTranscriber&) = delete;

    // UI thread. hold() before starting a thread that uses it, and
    // release() once that thread has ended, so the model isn't freed in
    // between.
    void hold();
    void release();

    // Any thread, while held. Like Transcriber's, but first waits for any
    // other thread's turn to end. Requesting stop on stop while waiting
    // throws TranscriptionCancelled too.
    std::string transcribe(const WhisperModel& model, std::span<const float> samples,
                           const std::string& language, std::stop_token stop);
    void prepare(const WhisperModel& model, std::stop_token stop);

private:
    Transcriber transcriber;
    // Whose turn it is, guarded by mutex
    std::mutex mutex;
    std::condition_variable_any turn_over;
    bool busy = false;
    // How many hold() without their release(). UI thread only.
    int holders = 0;

    // Waits for the turn, and ends it when it goes out of scope
    class Turn;

    // Static because FLTK callbacks are plain functions; the
    // SharedTranscriber comes in through data.
    static void on_unload_timeout(void* data);
};
