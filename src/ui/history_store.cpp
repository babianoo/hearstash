#include "ui/history_store.h"

#include <exception>
#include <format>
#include <iostream>
#include <utility>
#include <vector>

// FLTK
#include <FL/Fl.H>

#include "core/local_time.h"

// Seconds without a change before saving
static constexpr double SAVE_DELAY = 0.5;

HistoryStore::HistoryStore(const std::filesystem::path& file)
    : file(file), history(load_history(file)){
    // E.g. edited by hand. Saved the next time something changes.
    if (history.size() > CAPACITY) history.resize(CAPACITY);
}

HistoryStore::~HistoryStore(){
    Fl::remove_timeout(on_save_timeout, this);
    if (unsaved) save();
}

void HistoryStore::add(std::string query, Api api, std::optional<std::string> translation){
    HistoryEntry entry{
        .query = std::move(query),
        .api = api,
        .time = unix_now(),
        .translation = {},
    };
    if (api == Api::DeepL && translation && cacheable(*translation)) entry.translation = std::move(translation);

    // The same search made before. Replaced rather than kept: its
    // translation may be older than the new one, or too long to keep.
    std::erase_if(history, [&](const HistoryEntry& e){ return e.query == entry.query && e.api == entry.api; });

    history.insert(history.begin(), std::move(entry));
    if (history.size() > CAPACITY) history.pop_back();
    changed();
}

void HistoryStore::remove(std::string_view query, Api api){
    if (std::erase_if(history, [&](const HistoryEntry& e){ return e.query == query && e.api == api; }) > 0) changed();
}

const std::string* HistoryStore::cached_translation(std::string_view query) const {
    for (const HistoryEntry& entry : history)
        if (entry.api == Api::DeepL && entry.query == query) return entry.translation ? &*entry.translation : nullptr;
    return nullptr;
}

int HistoryStore::add_listener(std::function<void()> listener){
    listeners.emplace_back(next_listener_id, std::move(listener));
    return next_listener_id++;
}

void HistoryStore::remove_listener(int id){
    std::erase_if(listeners, [id](const auto& entry){ return entry.first == id; });
}

void HistoryStore::changed(){
    // A copy, so a listener can add or remove listeners
    const auto current = listeners;
    for (const auto& [id, listener] : current) listener();

    unsaved = true;
    // Restarts the wait, so a run of changes is saved once, after the last
    Fl::remove_timeout(on_save_timeout, this);
    Fl::add_timeout(SAVE_DELAY, on_save_timeout, this);
}

void HistoryStore::on_save_timeout(void* data){
    static_cast<HistoryStore*>(data)->save();
}

void HistoryStore::save(){
    try {
        save_history(file, history);
        unsaved = false;
    } catch (const std::exception& e){
        // Not in a dialog, which would pop up in the middle of searching
        std::cerr << std::format("Couldn't save the search history: {}\n", e.what());
    }
}
