#include "ui/frequency_downloader.h"

#include <chrono>
#include <exception>
#include <memory>

// FLTK
#include <FL/Fl.H>

// The least time between two progress updates, like DictionaryUpdater's
static constexpr auto UPDATE_INTERVAL = std::chrono::milliseconds(100);

using State = FrequencyDownloader::State;
using Status = FrequencyDownloader::Status;

FrequencyDownloader::FrequencyDownloader(std::function<void()> listener) : listener(std::move(listener)){
    start(Status::Loading, [](std::stop_token, const auto&){});
}

void FrequencyDownloader::download(){
    start(Status::Downloading, [](std::stop_token stop, const std::function<void(State)>& post){
        auto last_update = std::chrono::steady_clock::now();
        const auto progress = [&](std::uint64_t done, std::uint64_t total){
            // The SHA-1 is checked and the zip converted right after the
            // last bytes, which takes a few seconds with nothing to show
            if (total && done == total) return post({Status::Converting, 0, 0, {}});
            const auto now = std::chrono::steady_clock::now();
            if (now - last_update < UPDATE_INTERVAL) return;
            last_update = now;
            post({Status::Downloading, done, total, {}});
        };
        download_frequencies(progress, stop);
    });
}

void FrequencyDownloader::cancel(){
    thread.request_stop();
}

void FrequencyDownloader::remove(){
    start(Status::Loading, [](std::stop_token, const auto&){ delete_frequencies(); });
}

void FrequencyDownloader::start(Status status,
                                std::function<void(std::stop_token, const std::function<void(State)>&)> work){
    if (busy()) return;
    if (thread.joinable()) thread.join();
    state_ = {status, 0, 0, {}};
    if (listener) listener();

    thread = std::jthread([this, work = std::move(work)](std::stop_token stop){
        const auto post = [this](Update update){ Fl::awake(on_update, new Update(std::move(update))); };
        State end;
        try {
            work(stop, [&](State state){ post({.downloader = this, .state = std::move(state)}); });
        } catch (const DownloadCancelled&){
        } catch (const std::exception& e){
            end = {Status::Failed, 0, 0, e.what()};
        }
        Update last{.downloader = this, .state = std::move(end), .finished = true, .looked = true};
        if (const auto list = current_frequencies()) last.installed = list->info();
        post(std::move(last));
    });
}

void FrequencyDownloader::on_update(void* data){
    std::unique_ptr<Update> update(static_cast<Update*>(data));
    FrequencyDownloader* self = update->downloader;
    if (update->finished && self->thread.joinable()) self->thread.join();
    self->state_ = std::move(update->state);
    if (update->looked) self->installed_ = std::move(update->installed);
    if (self->listener) self->listener();
}
