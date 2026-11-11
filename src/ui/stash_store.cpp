#include "ui/stash_store.h"

#include <algorithm>
#include <exception>
#include <format>
#include <iostream>
#include <utility>

StashStore::StashStore(std::filesystem::path dir) : dir_(std::move(dir)), entries_(list_stash(dir_)){}

std::uintmax_t StashStore::bytes() const {
    std::uintmax_t total = 0;
    for (const StashEntry& entry : entries_) total += entry.bytes;
    return total;
}

StashEntry StashStore::add(const StashCapture& capture){
    try {
        StashEntry entry = stash_line(dir_, capture);
        // Newest first, and it's the newest
        entries_.insert(entries_.begin(), std::move(entry));
        last_error_.clear();
    } catch (const std::exception& e){
        last_error_ = e.what();
        changed();
        throw;
    }
    // A copy: a listener could change the list
    StashEntry added = entries_.front();
    changed();
    return added;
}

void StashStore::start_transcribing(const std::string& id){
    transcribing_.insert(id);
    changed();
}

void StashStore::finish_transcribing(const std::string& id, const std::string& transcript, const std::string& error){
    transcribing_.erase(id);
    const auto entry = std::ranges::find(entries_, id, &StashEntry::id);
    // Deleted while it was being transcribed
    if (entry == entries_.end()){
        changed();
        return;
    }
    try {
        set_stash_transcript(dir_, id, transcript, error);
        entry->transcript = transcript;
        entry->transcript_error = error;
    } catch (const std::exception& e){
        std::cerr << std::format("Couldn't save the transcript of the stashed line {}: {}\n", id, e.what());
    }
    changed();
}

void StashStore::set_last_error(std::string error){
    last_error_ = std::move(error);
    changed();
}

void StashStore::remove(std::string_view id){
    reread_after([&]{ delete_stashed(dir_, id); });
}

void StashStore::clear(){
    reread_after([&]{ clear_stash(dir_); });
}

void StashStore::reread_after(const std::function<void()>& change){
    // Read again even if it failed: part of a folder may be gone
    std::exception_ptr error;
    try {
        change();
    } catch (...){
        error = std::current_exception();
    }
    entries_ = list_stash(dir_);
    changed();
    if (error) std::rethrow_exception(error);
}

int StashStore::add_listener(std::function<void()> listener){
    listeners.emplace_back(next_listener_id, std::move(listener));
    return next_listener_id++;
}

void StashStore::remove_listener(int id){
    std::erase_if(listeners, [id](const auto& entry){ return entry.first == id; });
}

void StashStore::changed(){
    // A copy, so a listener can add or remove listeners
    const auto current = listeners;
    for (const auto& [id, listener] : current) listener();
}
