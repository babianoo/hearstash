#pragma once

#include <filesystem>
#include <functional>
#include <utility>
#include <vector>

#include "core/settings.h"

// Owns the app's Settings and keeps their file in step with them:
// everything reads them through get() and changes them through update(),
// which saves them shortly after.
//
// UI thread only, like the widgets. A search thread gets copies of what
// it needs instead.
class SettingsStore {
public:
    // Loads the settings from file, which is also where they're saved
    explicit SettingsStore(const std::filesystem::path& file);
    // Saves a change that's still waiting to be saved
    ~SettingsStore();

    // The timeout that saves holds a pointer to the store, so it can't be
    // copied. There's one per app anyway.
    SettingsStore(const SettingsStore&) = delete;
    SettingsStore& operator=(const SettingsStore&) = delete;

    const Settings& get() const { return settings; }

    // Runs change on the settings, then sanitizes them. They're saved half
    // a second after the last change, so typing a key writes the file
    // once, not on every keystroke.
    void update(const std::function<void(Settings&)>& change);

    // listener is called after every update(), e.g. to apply a change
    // right away. Returns an id for remove_listener. Remove a listener
    // before what it calls into dies.
    int add_listener(std::function<void()> listener);
    void remove_listener(int id);

private:
    std::filesystem::path file;
    Settings settings;
    // Changed since they were last saved
    bool unsaved = false;

    std::vector<std::pair<int, std::function<void()>>> listeners;
    int next_listener_id = 0;

    // Static because FLTK callbacks are plain functions; the store comes
    // in through data.
    static void on_save_timeout(void* data);
    // A failure is printed and the settings stay unsaved, so the next
    // change or the destructor tries again
    void save();
};
