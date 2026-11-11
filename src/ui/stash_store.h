#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/stash.h"

// Owns the list of stashed lines (core/stash.h) and keeps it in step with
// the stash folder, like HistoryStore does for the history: the stash
// shortcut adds to it, and the stash window shows, opens and deletes.
//
// UI thread only, like the widgets.
class StashStore {
public:
    // Lists the lines in dir, which is where new ones are saved
    explicit StashStore(std::filesystem::path dir);

    // There's one per app, and listeners hold pointers to it
    StashStore(const StashStore&) = delete;
    StashStore& operator=(const StashStore&) = delete;

    const std::filesystem::path& dir() const { return dir_; }
    // Newest first
    const std::vector<StashEntry>& entries() const { return entries_; }
    // What they take on disk together
    std::uintmax_t bytes() const;

    // Saves capture as a new line, and returns it. Throws
    // std::runtime_error or std::filesystem::filesystem_error if it can't;
    // then last_error() says why too, until a stash works.
    StashEntry add(const StashCapture& capture);

    // Whether the line with id is being transcribed, which the stash
    // window says in place of its transcript
    bool transcribing(std::string_view id) const { return transcribing_.contains(std::string(id)); }
    // Marks the line with id as being transcribed now
    void start_transcribing(const std::string& id);
    // Saves the line's transcript, or why there's none, once it's done.
    // A line deleted meanwhile is left alone. Never throws: a transcript
    // that can't be saved is printed, and the line opens without it.
    void finish_transcribing(const std::string& id, const std::string& transcript, const std::string& error);
    // Why the last stash failed, e.g. with the buffer off. Empty if it
    // worked. Pressed while watching, it can't be said in a dialog.
    const std::string& last_error() const { return last_error_; }
    // Records why a stash failed before anything could be saved
    void set_last_error(std::string error);

    // Deletes the line with id. Throws std::filesystem::filesystem_error
    // if it can't be; the list is read again from the folder anyway.
    void remove(std::string_view id);
    // Deletes every line. Throws like remove().
    void clear();

    // listener is called after the entries or last_error() change.
    // Returns an id for remove_listener. Remove a listener before what it
    // calls into dies.
    int add_listener(std::function<void()> listener);
    void remove_listener(int id);

private:
    std::filesystem::path dir_;
    std::vector<StashEntry> entries_;
    std::string last_error_;
    // By id
    std::set<std::string> transcribing_;

    std::vector<std::pair<int, std::function<void()>>> listeners;
    int next_listener_id = 0;

    // Tells the listeners
    void changed();
    // Runs change, then reads the list again from the folder and tells the
    // listeners, even if change threw, which is then thrown on
    void reread_after(const std::function<void()>& change);
};
