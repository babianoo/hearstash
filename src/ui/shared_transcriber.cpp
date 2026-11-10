#include "ui/shared_transcriber.h"

// FLTK
#include <FL/Fl.H>

class SharedTranscriber::Turn {
public:
    Turn(SharedTranscriber& owner, std::stop_token stop) : owner(owner){
        std::unique_lock lock(owner.mutex);
        // False if stopped first
        if (!owner.turn_over.wait(lock, stop, [this]{ return !this->owner.busy; }))
            throw TranscriptionCancelled();
        owner.busy = true;
    }

    ~Turn(){
        {
            std::lock_guard lock(owner.mutex);
            owner.busy = false;
        }
        owner.turn_over.notify_all();
    }

    Turn(const Turn&) = delete;
    Turn& operator=(const Turn&) = delete;

private:
    SharedTranscriber& owner;
};

SharedTranscriber::~SharedTranscriber(){
    Fl::remove_timeout(on_unload_timeout, this);
}

void SharedTranscriber::hold(){
    ++holders;
    Fl::remove_timeout(on_unload_timeout, this);
}

void SharedTranscriber::release(){
    if (--holders == 0) Fl::add_timeout(UNLOAD_AFTER, on_unload_timeout, this);
}

std::string SharedTranscriber::transcribe(const WhisperModel& model, bool gpu, std::span<const float> samples,
                                          const std::string& language, std::stop_token stop){
    Turn turn(*this, stop);
    return transcriber.transcribe(model, gpu, samples, language, stop);
}

void SharedTranscriber::prepare(const WhisperModel& model, bool gpu, std::stop_token stop){
    Turn turn(*this, stop);
    transcriber.prepare(model, gpu);
}

void SharedTranscriber::on_unload_timeout(void* data){
    SharedTranscriber* self = static_cast<SharedTranscriber*>(data);
    // Removed by hold(), and only added back once nothing holds it, so no
    // thread is using the model
    self->transcriber.unload();
}
