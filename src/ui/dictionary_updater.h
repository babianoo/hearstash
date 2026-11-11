#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/dictionary_files.h"

class SettingsStore;

// Keeps the dictionary current: looks for a newer one than what's
// installed, and downloads and converts it, on a thread of its own, telling
// the UI how it's going. Owned by main(), not a window, so an update keeps
// going while the settings window is closed. One thing at a time.
//
// The dictionary's data is licensed on the condition that an app using it
// keeps it up to date (see dictionary_files.h), so the app doesn't leave
// that to the user remembering: check_at_start() looks at the dictionary's
// age every time the app starts.
//
// UI thread only, like the widgets: its thread hands everything back
// through Fl::awake(), so what it reports only changes between callbacks.
class DictionaryUpdater {
public:
    enum class Status { Idle, Checking, Downloading, Converting, Failed };

    struct State {
        Status status = Status::Idle;
        // While Downloading, bytes, and total is 0 until the server sends
        // it. While Converting, how many of the dictionary's parts.
        std::uint64_t done = 0;
        std::uint64_t total = 0;
        // Why it failed, while Failed
        std::string error;
        // While Idle: the last check found nothing newer than what's
        // installed
        bool up_to_date = false;
    };

    // settings has to outlive it
    explicit DictionaryUpdater(SettingsStore& settings) : settings(settings){}
    // Cancels what's running and waits for it to stop, which takes at most
    // about a second
    ~DictionaryUpdater() = default;

    // Its thread holds a pointer to it. There's one per app anyway.
    DictionaryUpdater(const DictionaryUpdater&) = delete;
    DictionaryUpdater& operator=(const DictionaryUpdater&) = delete;

    // Looks at the installed dictionary. If it's older than
    // DICTIONARY_MAX_AGE_DAYS, or there's none, asks its site for a newer
    // one, and if there is one asks the user whether to get it now, unless
    // they said "later" in the last week. Says nothing if the site can't
    // be reached: the app may just be offline.
    void check_at_start();
    // Looks for a newer dictionary and installs it without asking: the
    // user pressed the button. Does nothing while busy.
    void update();
    // Stops what's running. It stays busy until its thread has stopped.
    void cancel();

    const State& state() const { return state_; }
    bool busy() const { return state_.status != Status::Idle && state_.status != Status::Failed; }
    // What's installed and where, as of the last time the thread looked:
    // at start, and after every update. Neither before the first look, or
    // when there's no dictionary.
    const std::optional<DictionaryInfo>& installed() const { return installed_; }
    const std::filesystem::path& installed_path() const { return installed_path_; }

    // listener is called on the UI thread whenever any of the above
    // changes. Returns an id for remove_listener; remove a listener before
    // what it calls into dies.
    int add_listener(std::function<void()> listener);
    void remove_listener(int id);

private:
    // What the thread hands back to the UI thread
    struct Update {
        DictionaryUpdater* updater;
        State state;
        // The thread's last update: it's about to end
        bool finished = false;
        // Whether installed is what the thread just found installed
        bool looked = false;
        std::optional<DictionaryInfo> installed{};
        std::filesystem::path installed_path{};
        // A newer one to ask the user about
        std::optional<DictionaryRelease> offer{};
    };

    SettingsStore& settings;
    State state_;
    std::optional<DictionaryInfo> installed_;
    std::filesystem::path installed_path_;
    // Joinable while busy. A jthread requests stop and joins when it dies,
    // which is what the destructor relies on.
    std::jthread thread;

    std::vector<std::pair<int, std::function<void()>>> listeners;
    int next_listener_id = 0;

    // At start: only if the dictionary is old, and only so far as finding
    // a newer one, which may_offer says whether to ask the user about.
    // With release: installs that one without looking for it.
    void start(bool at_start, bool may_offer, std::optional<DictionaryRelease> release);
    // Runs on the UI thread, called by Fl::awake() from the thread
    static void on_update(void* data);
    // Asks whether to install release, and does if so
    void offer(const DictionaryRelease& release);
    void notify();
};
