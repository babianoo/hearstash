#pragma once

#include <string>
#include <thread>
#include <vector>

// Only references to these are stored here, so the full headers are
// included in stash_transcriber.cpp instead.
class SettingsStore;
class SharedTranscriber;
class StashStore;

// Transcribes stashed lines in the background as they're stashed, if the
// settings say to, so the stash window can show what each one says and
// opening one shows its transcript straight away. Each line's selection
// (the end the capture window selects at first) is transcribed in turn
// with the shared model, which the capture window and the search bar wait
// for meanwhile.
//
// UI thread only, like the widgets.
class StashTranscriber {
public:
    // settings, transcriber and stash have to outlive it
    StashTranscriber(SettingsStore& settings, SharedTranscriber& transcriber, StashStore& stash);
    // Stops the transcriptions still running and waits for them
    ~StashTranscriber();

    // Its jobs hold a pointer to it. There's one per app.
    StashTranscriber(const StashTranscriber&) = delete;
    StashTranscriber& operator=(const StashTranscriber&) = delete;

    // Starts transcribing clip, rate a second, the selection of the line
    // with id, which has just been stashed. Without a whisper model ready,
    // saves that as the reason there's no transcript.
    void transcribe(const std::string& id, std::vector<float> clip, int rate);

private:
    // A thread that transcribes. It hands its result back through
    // Fl::awake(), even when stopped, so it can be joined then.
    struct Job {
        unsigned id;
        std::jthread thread;
    };
    // What a job hands back to the UI thread
    struct JobResult {
        StashTranscriber* self;
        unsigned job;
        std::string line;
        std::string text{};
        std::string error{};    // Non-empty if it failed
        bool cancelled = false;
    };

    SettingsStore& settings;
    SharedTranscriber& transcriber;
    StashStore& stash;
    std::vector<Job> jobs;
    unsigned next_job_id = 1;

    // Runs on the UI thread, called by Fl::awake() from a job
    static void on_job_done(void* data);
};
