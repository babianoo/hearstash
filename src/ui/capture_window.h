#pragma once

#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// FLTK
#include <FL/Fl_Double_Window.H>

#include "core/image.h"
#include "core/jisho.h"

// Only pointers and references to these are stored here, so the full
// headers are included in capture_window.cpp instead.
class CardWindow;
class ClipPlayer;
class SettingsStore;
class SharedTranscriber;
class SmoothScroll;
class Waveform;
class WrappedText;
class Fl_Box;
class Fl_Button;
class Fl_Widget;
struct TextSpan;

// The window a capture opens in: the waveform of the last seconds of
// what the computer played, with the end already selected. The selection
// can be changed by dragging, and played back with Space or the Play
// button. Enter (or the Transcribe button) turns it into text with
// whisper, shown under the waveform; Escape then cancels. Selecting part
// of the text with the mouse looks it up on Jisho, in a panel beside it.
//
// The whisper model starts loading as soon as a capture opens, so it's
// mostly ready by the time the selection is.
//
// Closing it (or Escape) hides it, stops playing and transcribing, and
// forgets the audio.
class CaptureWindow {
public:
    // settings and transcriber have to outlive the window
    CaptureWindow(SettingsStore& settings, SharedTranscriber& transcriber);
    // Defined in capture_window.cpp, where ClipPlayer and CardWindow are
    // complete, which unique_ptr needs to delete them. Stops the threads
    // still running and waits for them.
    ~CaptureWindow();

    // One capture window per app: copying makes no sense
    CaptureWindow(const CaptureWindow&) = delete;
    CaptureWindow& operator=(const CaptureWindow&) = delete;

    // Shows audio, rate a second, in place of the capture there was, with
    // the last selection_seconds of it selected, and brings the window to
    // the front. Cancels a transcription of the capture there was.
    // screenshot is kept for the card; if it's empty, screenshot_error
    // says why.
    void open(std::vector<float> audio, int rate, int selection_seconds, Image screenshot,
              std::string screenshot_error);

private:
    // A thread that loads the model or transcribes. Each one hands its
    // result back through Fl::awake(), even when stopped, so it can be
    // joined then.
    struct Job {
        unsigned id;
        std::jthread thread;
    };
    // What a lookup thread hands back to the UI thread
    struct LookupResult {
        CaptureWindow* win;
        unsigned id;
        std::string keyword;
        std::vector<JishoEntry> entries{};
        std::string error{};    // Non-empty if it failed
    };
    // What a job hands back to the UI thread
    struct JobResult {
        CaptureWindow* win;
        unsigned id;
        std::string text{};
        std::string error{};    // Non-empty if it failed
        bool cancelled = false;
    };

    SettingsStore& settings;
    SharedTranscriber& transcriber;

    // Owned by CaptureWindow as a member, so no new/delete anywhere
    Fl_Double_Window win{900, 560, "Capture"};

    // Created with new in the constructor and added to win, which deletes
    // them when it dies
    Waveform* waveform = nullptr;
    Fl_Button* play_btn = nullptr;
    // How long the selection is
    Fl_Box* selection_info = nullptr;
    Fl_Button* transcribe_btn = nullptr;
    Fl_Button* anki_btn = nullptr;
    // The transcript, or what to do to get one. transcript_text is
    // transcript's only child.
    SmoothScroll* transcript = nullptr;
    WrappedText* transcript_text = nullptr;
    // What was selected in the transcript, looked up. dictionary_text is
    // dictionary's only child.
    SmoothScroll* dictionary = nullptr;
    WrappedText* dictionary_text = nullptr;

    // What waveform shows; it only borrows it
    std::vector<float> audio;
    int rate = 1;
    // Created on the first play, so a machine without audio output only
    // finds out then
    std::unique_ptr<ClipPlayer> player;
    Image screenshot;
    std::string screenshot_error;
    // The transcript shown, and the first entry of the lookup shown, if
    // any, for a card
    std::string transcript_shown;
    std::optional<JishoEntry> entry_shown;
    std::string keyword_shown;
    // Created the first time a card is added. Not a plain member: a window
    // created in this one's constructor would end up inside it.
    std::unique_ptr<CardWindow> card_win;

    // Running, or done and not joined yet. Usually one or two.
    std::vector<Job> jobs;
    unsigned next_job_id = 1;
    // The job whose transcript is awaited, or 0 if none
    unsigned transcribing = 0;
    // Bumped on every lookup. Only the latest one's result is shown.
    unsigned latest_lookup = 0;

    // Static because FLTK callbacks are plain functions; the CaptureWindow
    // comes in through data.
    static void on_play(Fl_Widget* w, void* data);
    static void on_transcribe(Fl_Widget* w, void* data);
    static void on_add_to_anki(Fl_Widget* w, void* data);
    // The window's close button and Escape
    static void on_close(Fl_Widget* w, void* data);
    // Many times a second while playing: moves the playhead, and notices
    // the end
    static void on_play_tick(void* data);
    // Runs on the UI thread, called by Fl::awake() from a job
    static void on_job_done(void* data);
    // Runs on the UI thread, called by Fl::awake() from a lookup thread
    static void on_lookup_done(void* data);

    void play();
    void stop_playing();
    // Shows the selection's length, and whether there's one to act on
    void refresh_selection();
    // Starts loading the chosen model, if there's one ready to load
    void prepare_model();
    void transcribe();
    // Stops waiting for the transcript there is, if there is one
    void cancel_transcription();
    // Shows the Transcribe and Add to Anki buttons as they should be now
    void refresh_transcribe_button();
    // Opens the card window with what's shown
    void add_to_anki();
    // Replaces what the transcript area shows
    void show_transcript(const std::vector<TextSpan>& spans);
    void show_note(const std::string& note);
    // Looks up what's selected in the transcript, if it's worth it
    void look_up_selection();
    // Replaces what the lookup panel shows
    void show_lookup(const std::vector<TextSpan>& spans);
};
