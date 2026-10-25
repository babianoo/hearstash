#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/whisper_models.h"

// Downloads whisper models on threads of their own and tells the UI how
// they're going. Owned by main(), not a window, so a download keeps going
// while the settings window is closed. Several models can download at
// once, each at most once at a time.
//
// UI thread only, like the widgets: its threads hand their progress back
// through Fl::awake(), so what it reports only changes between callbacks.
class ModelDownloader {
public:
    // Whether a model is installed is is_installed()'s to say, so a
    // finished download is Idle again
    enum class Status { Idle, Downloading, Failed };

    struct State {
        Status status = Status::Idle;
        // While Downloading. total is 0 until the server sends the size.
        std::uint64_t received = 0;
        std::uint64_t total = 0;
        // Why it failed, while Failed
        std::string error;
    };

    // Called on the UI thread when a model's State changes, and when its
    // download finishes, after which it's installed
    using Listener = std::function<void(const WhisperModel& model)>;

    ModelDownloader() = default;
    // Cancels the downloads still running and waits for them to stop,
    // which deletes their .part files. That takes at most about a second.
    ~ModelDownloader() = default;

    // Its threads hold a pointer to it, so it can't be copied. There's one
    // per app anyway.
    ModelDownloader(const ModelDownloader&) = delete;
    ModelDownloader& operator=(const ModelDownloader&) = delete;

    // Starts downloading model, replacing a copy that's installed. Does
    // nothing if it's already downloading.
    void start(const WhisperModel& model);
    // Stops model's download. It stays Downloading until its thread has
    // stopped, then is Idle. Does nothing if it isn't downloading.
    void cancel(const WhisperModel& model);

    const State& state(const WhisperModel& model) const { return states[index_of(model)]; }

    // Returns an id for remove_listener. Remove a listener before what it
    // calls into dies.
    int add_listener(Listener listener);
    void remove_listener(int id);

private:
    // What a download thread hands back to the UI thread
    struct Update {
        ModelDownloader* downloader;
        size_t index;
        State state;
        // The thread's last update: it's about to end
        bool finished;
    };

    // Indexed like WHISPER_MODELS
    std::array<State, WHISPER_MODELS.size()> states{};
    // Joinable while downloading. A jthread requests stop and joins when
    // it dies, which is what the destructor relies on.
    std::array<std::jthread, WHISPER_MODELS.size()> threads{};

    std::vector<std::pair<int, Listener>> listeners;
    int next_listener_id = 0;

    // Where model is in WHISPER_MODELS. It has to be one of them.
    static size_t index_of(const WhisperModel& model);
    // Runs on the UI thread, called by Fl::awake() from a download thread
    static void on_update(void* data);
    void notify(const WhisperModel& model);
};
