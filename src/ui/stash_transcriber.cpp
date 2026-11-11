#include "ui/stash_transcriber.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <stop_token>
#include <utility>

// FLTK
#include <FL/Fl.H>

#include "core/resample.h"
#include "core/settings.h"
#include "core/transcriber.h"
#include "core/whisper_models.h"
#include "ui/settings_store.h"
#include "ui/shared_transcriber.h"
#include "ui/stash_store.h"

StashTranscriber::StashTranscriber(SettingsStore& settings, SharedTranscriber& transcriber, StashStore& stash)
    : settings(settings), transcriber(transcriber), stash(stash){}

StashTranscriber::~StashTranscriber(){
    // jobs then goes with the members, stopping and joining each thread.
    // Their results are never handled: Fl::run() has returned by now.
    for (Job& job : jobs) job.thread.request_stop();
}

void StashTranscriber::transcribe(const std::string& id, std::vector<float> clip, int rate){
    const WhisperModel* model = find_model(settings.get().speech.whisper_model);
    if (!model || !is_installed(*model)){
        stash.finish_transcribing(id, "", "No whisper model is ready. Choose one in Settings > Speech-to-Text.");
        return;
    }

    const bool gpu = settings.get().speech.use_gpu;
    const unsigned job_id = next_job_id++;
    stash.start_transcribing(id);
    transcriber.hold();
    // model points into WHISPER_MODELS, which lives for the whole program.
    // Waits its turn behind another line, or the capture window.
    jobs.push_back({job_id, std::jthread([this, job_id, id, model, gpu, clip = std::move(clip), rate](std::stop_token stop){
        auto* result = new JobResult{.self = this, .job = job_id, .line = id};
        try {
            const std::vector<float> samples = resample(clip, rate, Transcriber::SAMPLE_RATE);
            result->text = transcriber.transcribe(*model, gpu, samples, "ja", stop);
            if (result->text.empty()) result->error = "No speech found in the selection";
        } catch (const TranscriptionCancelled&){
            result->cancelled = true;
        } catch (const std::exception& e){
            result->error = e.what();
        }
        Fl::awake(on_job_done, result);
    })});
}

void StashTranscriber::on_job_done(void* data){
    // unique_ptr so result is deleted when this returns
    std::unique_ptr<JobResult> result(static_cast<JobResult*>(data));
    StashTranscriber* self = result->self;

    // Its last act was queueing this, so joining takes no time
    const auto job = std::ranges::find(self->jobs, result->job, &Job::id);
    if (job != self->jobs.end()){
        job->thread.join();
        self->jobs.erase(job);
    }
    self->transcriber.release();

    // Only stopped when the app quits
    if (result->cancelled) return;
    self->stash.finish_transcribing(result->line, result->text, result->error);
}
