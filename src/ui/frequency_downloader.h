#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <thread>

#include "core/frequency_list.h"

// Gets the frequency list (frequency_list.h) when the user asks for it,
// and deletes it, on a thread of its own, telling the UI how it's going.
// Owned by the settings window, which lives as long as the app, so a
// download keeps going while it's closed.
//
// UI thread only, like the widgets: its thread hands everything back
// through Fl::awake(), so what it reports only changes between callbacks.
class FrequencyDownloader {
public:
    enum class Status { Idle, Loading, Downloading, Converting, Failed };

    struct State {
        Status status = Status::Idle;
        // While Downloading: bytes, and total is 0 until the server sends it
        std::uint64_t done = 0;
        std::uint64_t total = 0;
        // Why it failed, while Failed
        std::string error;
    };

    // Opens the installed list on its thread, so the first lookup doesn't
    // wait for that. listener is called on the UI thread whenever any of
    // what's below changes; it has to outlive this.
    explicit FrequencyDownloader(std::function<void()> listener);
    // Cancels a download and waits for it to stop
    ~FrequencyDownloader() = default;

    FrequencyDownloader(const FrequencyDownloader&) = delete;
    FrequencyDownloader& operator=(const FrequencyDownloader&) = delete;

    // Downloads and converts it. Does nothing while busy.
    void download();
    // Stops a download. It stays busy until its thread has stopped.
    void cancel();
    // Deletes the installed list. Does nothing while busy.
    void remove();

    const State& state() const { return state_; }
    bool busy() const { return state_.status != Status::Idle && state_.status != Status::Failed; }
    // What's installed, as of the last time the thread looked
    const std::optional<FrequencyInfo>& installed() const { return installed_; }

private:
    struct Update {
        FrequencyDownloader* downloader;
        State state;
        bool finished = false;
        bool looked = false;
        std::optional<FrequencyInfo> installed{};
    };

    std::function<void()> listener;
    State state_;
    std::optional<FrequencyInfo> installed_;
    std::jthread thread;

    // Runs work on the thread, then looks at what's installed
    void start(Status status, std::function<void(std::stop_token, const std::function<void(State)>&)> work);
    static void on_update(void* data);
};
