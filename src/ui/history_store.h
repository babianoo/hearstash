#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/api.h"
#include "core/history.h"

// Owns the search history and keeps its file in step with it, like
// SettingsStore does for the settings: changes are saved shortly after.
//
// Keeps the newest CAPACITY searches, one entry per query and API: a
// search made again moves its entry back to the top.
//
// UI thread only, like the widgets. A search thread hands its result back
// to the UI thread, which then adds it.
class HistoryStore {
public:
    static constexpr size_t CAPACITY = 100;

    // Loads the history from file, which is also where it's saved
    explicit HistoryStore(const std::filesystem::path& file);
    // Saves a change that's still waiting to be saved
    ~HistoryStore();

    // The timeout that saves holds a pointer to the store, so it can't be
    // copied. There's one per app anyway.
    HistoryStore(const HistoryStore&) = delete;
    HistoryStore& operator=(const HistoryStore&) = delete;

    // Newest first
    const std::vector<HistoryEntry>& entries() const { return history; }

    // Records a search that just succeeded, stamped with the time now.
    // translation is DeepL's, which is kept if it's cacheable().
    void add(std::string query, Api api, std::optional<std::string> translation = {});

    // DeepL's translation of exactly query from an earlier search, or
    // nullptr if there's none kept. Only valid until the history changes.
    const std::string* cached_translation(std::string_view query) const;

    // listener is called after the entries change, e.g. to show them.
    // Returns an id for remove_listener. Remove a listener before what it
    // calls into dies.
    int add_listener(std::function<void()> listener);
    void remove_listener(int id);

private:
    std::filesystem::path file;
    std::vector<HistoryEntry> history;
    // Changed since it was last saved
    bool unsaved = false;

    std::vector<std::pair<int, std::function<void()>>> listeners;
    int next_listener_id = 0;

    // Tells the listeners, and saves half a second after the last change,
    // so a run of changes is saved once
    void changed();

    // Static because FLTK callbacks are plain functions; the store comes
    // in through data.
    static void on_save_timeout(void* data);
    // A failure is printed and the history stays unsaved, so the next
    // change or the destructor tries again
    void save();
};
