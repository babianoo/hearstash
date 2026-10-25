#include "ui/model_downloader.h"

#include <cassert>
#include <chrono>
#include <exception>
#include <memory>

// FLTK
#include <FL/Fl.H>

// The least time between two progress updates. curl reports hundreds of
// times a second, far more than a progress bar needs, and each update is
// a message for the UI thread to handle.
static constexpr auto UPDATE_INTERVAL = std::chrono::milliseconds(100);

size_t ModelDownloader::index_of(const WhisperModel& model){
    assert(&model >= WHISPER_MODELS.data() && &model < WHISPER_MODELS.data() + WHISPER_MODELS.size());
    return static_cast<size_t>(&model - WHISPER_MODELS.data());
}

void ModelDownloader::start(const WhisperModel& model){
    const size_t i = index_of(model);
    if (states[i].status == Status::Downloading) return;

    states[i] = State{Status::Downloading, 0, 0, {}};
    notify(model);

    // The thread gets what it needs by value, and hands everything back
    // through Fl::awake(); it never touches states itself. The stop_token
    // is the jthread's own, which cancel() and the destructor stop.
    threads[i] = std::jthread([this, i](std::stop_token stop){
        const WhisperModel& model = WHISPER_MODELS[i];
        auto last_update = std::chrono::steady_clock::now();

        auto progress = [&](std::uint64_t received, std::uint64_t total){
            const auto now = std::chrono::steady_clock::now();
            if (now - last_update < UPDATE_INTERVAL) return;
            last_update = now;
            Fl::awake(on_update, new Update{this, i, {Status::Downloading, received, total, {}}, false});
        };

        State result;   // Idle: done or cancelled
        try {
            download_model(model, progress, stop);
        } catch (const DownloadCancelled&){
            // Idle, like before it started
        } catch (const std::exception& e){
            result = State{Status::Failed, 0, 0, e.what()};
        }

        // Queues on_update to run on the UI thread, which then owns the
        // Update. Widgets may only be changed from there.
        Fl::awake(on_update, new Update{this, i, std::move(result), true});
    });
}

void ModelDownloader::cancel(const WhisperModel& model){
    // Does nothing if the thread has already ended or never started
    threads[index_of(model)].request_stop();
}

void ModelDownloader::on_update(void* data){
    // unique_ptr so update is deleted when this returns
    std::unique_ptr<Update> update(static_cast<Update*>(data));
    ModelDownloader* self = update->downloader;
    const size_t i = update->index;

    // Its last act was queueing this, so it's ending, if it hasn't
    // already: joining takes no time. FLTK runs these in the order they
    // were queued, so no progress update can come after this one.
    if (update->finished) self->threads[i].join();

    self->states[i] = std::move(update->state);
    self->notify(WHISPER_MODELS[i]);
}

int ModelDownloader::add_listener(Listener listener){
    listeners.emplace_back(next_listener_id, std::move(listener));
    return next_listener_id++;
}

void ModelDownloader::remove_listener(int id){
    std::erase_if(listeners, [id](const auto& entry){ return entry.first == id; });
}

void ModelDownloader::notify(const WhisperModel& model){
    // A copy, so a listener can add or remove listeners
    const auto current = listeners;
    for (const auto& [id, listener] : current) listener(model);
}
