#include "ui/dictionary_updater.h"

#include <chrono>
#include <exception>
#include <format>
#include <memory>

// FLTK
#include <FL/Fl.H>
#include <FL/fl_ask.H>

#include "core/frequency_list.h"
#include "core/settings.h"
#include "core/word_lookup.h"
#include "ui/settings_store.h"

// The least time between two progress updates, like ModelDownloader's
static constexpr auto UPDATE_INTERVAL = std::chrono::milliseconds(100);
// How long "later" lasts
static constexpr std::int64_t ASK_AGAIN_AFTER = 7 * 24 * 60 * 60;

static std::int64_t now_seconds(){
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

using State = DictionaryUpdater::State;
using Status = DictionaryUpdater::Status;

// Doing status, done of total through it
static State busy_state(Status status, std::uint64_t done = 0, std::uint64_t total = 0){
    return {status, done, total, {}, false};
}

// Done, or never started
static State idle_state(bool up_to_date = false){
    return {Status::Idle, 0, 0, {}, up_to_date};
}

void DictionaryUpdater::check_at_start(){
    start(true, now_seconds() - settings.get().dictionary.update_declined >= ASK_AGAIN_AFTER, std::nullopt);
}

void DictionaryUpdater::update(){
    start(false, false, std::nullopt);
}

void DictionaryUpdater::cancel(){
    // Does nothing if the thread has already ended or never started
    thread.request_stop();
}

void DictionaryUpdater::start(bool at_start, bool may_offer, std::optional<DictionaryRelease> release){
    if (busy()) return;
    // One that ended by handing back its last update, which is still on
    // its way: its state can't be told from idle, so wait for it here
    if (thread.joinable()) thread.join();

    state_ = busy_state(Status::Checking);
    notify();

    // The thread gets what it needs by value, and hands everything back
    // through Fl::awake(); it never touches this object's state itself
    thread = std::jthread([this, at_start, may_offer, release = std::move(release)](std::stop_token stop) mutable {
        const auto post = [this](Update update){ Fl::awake(on_update, new Update(std::move(update))); };
        const auto finish = [&](State state, std::optional<DictionaryRelease> offer = std::nullopt){
            post({.updater = this, .state = std::move(state), .finished = true, .offer = std::move(offer)});
        };

        // Opens the dictionary if nothing has yet, which takes a moment
        std::shared_ptr<const Dictionary> dictionary = current_dictionary();
        // And the frequency list, for the same reason
        current_frequencies();
        const auto post_installed = [&](State state){
            Update update{.updater = this, .state = std::move(state), .looked = true};
            if (dictionary){
                update.installed = dictionary->info();
                update.installed_path = dictionary->path();
            }
            post(std::move(update));
        };

        try {
            if (!release){
                // Young enough: nothing to do until it's older
                if (at_start && dictionary && dictionary_age_days(dictionary->info()) < DICTIONARY_MAX_AGE_DAYS){
                    post_installed(idle_state());
                    return finish(idle_state());
                }
                post_installed(busy_state(Status::Checking));
                if (at_start && !may_offer) return finish(idle_state());

                DictionaryRelease latest;
                try {
                    latest = latest_dictionary_release(dictionary ? &dictionary->info() : nullptr);
                } catch (const std::exception&){
                    // Offline, most likely. Unasked, that's not worth a
                    // word; asked, it is.
                    if (at_start) return finish(idle_state());
                    throw;
                }
                if (!is_newer(latest, dictionary ? &dictionary->info() : nullptr))
                    return finish(idle_state(true));
                // The user decides, on the UI thread
                if (at_start) return finish(idle_state(), std::move(latest));
                release = std::move(latest);
            }

            auto last_update = std::chrono::steady_clock::now();
            const auto progress = [&](Status status){
                return [&, status](std::uint64_t done, std::uint64_t total){
                    const auto now = std::chrono::steady_clock::now();
                    if (now - last_update < UPDATE_INTERVAL) return;
                    last_update = now;
                    post({.updater = this, .state = busy_state(status, done, total)});
                };
            };
            post({.updater = this, .state = busy_state(Status::Downloading)});
            update_dictionary(*release, progress(Status::Downloading), progress(Status::Converting), stop);

            // From here on lookups use the new one
            reload_dictionary();
            dictionary = current_dictionary();
            post_installed(idle_state(true));
            finish(idle_state(true));
        } catch (const DownloadCancelled&){
            finish(idle_state());
        } catch (const std::exception& e){
            finish({Status::Failed, 0, 0, e.what(), false});
        }
    });
}

void DictionaryUpdater::on_update(void* data){
    // unique_ptr so update is deleted when this returns
    std::unique_ptr<Update> update(static_cast<Update*>(data));
    DictionaryUpdater* self = update->updater;

    // Its last act was queueing this, so it's ending, if it hasn't
    // already: joining takes no time. FLTK runs these in the order they
    // were queued, so no progress update can come after this one.
    if (update->finished && self->thread.joinable()) self->thread.join();

    self->state_ = std::move(update->state);
    if (update->looked){
        self->installed_ = std::move(update->installed);
        self->installed_path_ = std::move(update->installed_path);
    }
    self->notify();
    if (update->offer) self->offer(*update->offer);
}

void DictionaryUpdater::offer(const DictionaryRelease& release){
    const std::string name = release.title.empty() ? "revision " + release.revision : release.title;
    const std::string message = installed_
        ? std::format("A newer dictionary is out: {}.\n\n"
                      "The one installed is {} days old, and its licence asks that it's kept up to date.\n"
                      "Get the new one now? It's about 40 MB, and takes a minute or two.",
                      name, dictionary_age_days(*installed_))
        : std::format("No dictionary is installed, so words are looked up on jisho.org.\n\n"
                      "Get {} now? It's about 40 MB, and takes a minute or two.", name);
    // Modal: waits for an answer. 0 is the first button, and Escape.
    if (fl_choice("%s", "Later", "Get it", nullptr, message.c_str()) == 1){
        start(false, false, release);
    } else {
        settings.update([](Settings& s){ s.dictionary.update_declined = now_seconds(); });
    }
}

int DictionaryUpdater::add_listener(std::function<void()> listener){
    listeners.emplace_back(next_listener_id, std::move(listener));
    return next_listener_id++;
}

void DictionaryUpdater::remove_listener(int id){
    std::erase_if(listeners, [id](const auto& entry){ return entry.first == id; });
}

void DictionaryUpdater::notify(){
    // A copy, so a listener can add or remove listeners
    const auto current = listeners;
    for (const auto& [id, listener] : current) listener();
}
