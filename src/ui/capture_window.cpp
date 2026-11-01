#include "ui/capture_window.h"

#include <algorithm>
#include <exception>
#include <format>
#include <span>

// FLTK
#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>

#include "core/clip_player.h"
#include "core/resample.h"
#include "core/settings.h"
#include "core/transcriber.h"
#include "core/whisper_models.h"
#include "ui/settings_store.h"
#include "ui/shared_transcriber.h"
#include "ui/smooth_scroll.h"
#include "ui/theme.h"
#include "ui/waveform.h"
#include "ui/wrapped_text.h"

// Space around and between things
static constexpr int MARGIN = 10;
static constexpr int WAVEFORM_H = 160;
static constexpr int BTN_W = 90, BTN_H = 28;
static constexpr int TRANSCRIBE_W = 130;
// Seconds between playhead moves while playing
static constexpr double PLAY_TICK = 1.0 / 30;

// The transcript is what the window is for, so it's big. Notes, like what
// to press, are smaller and grey.
static const TextStyle TRANSCRIPT{FL_HELVETICA, 24};
static const TextStyle NOTE{FL_HELVETICA, 14, DIM};

CaptureWindow::CaptureWindow(SettingsStore& settings, SharedTranscriber& transcriber)
    : settings(settings), transcriber(transcriber){
    // win is the current group here, like in MainWindow
    const int inner_w = win.w() - 2 * MARGIN;
    waveform = new Waveform(MARGIN, MARGIN, inner_w, WAVEFORM_H);
    waveform->on_selection([this]{
        // What plays is the selection as it was
        stop_playing();
        refresh_selection();
    });

    const int row_y = MARGIN + WAVEFORM_H + MARGIN;
    play_btn = new Fl_Button(MARGIN, row_y, BTN_W, BTN_H, "Play");
    play_btn->box(FL_ROUND_UP_BOX);
    play_btn->clear_visible_focus();
    play_btn->shortcut(' ');
    play_btn->tooltip("Play the selection (Space)");
    play_btn->callback(on_play, this);

    const int transcribe_x = MARGIN + inner_w - TRANSCRIBE_W;
    selection_info = new Fl_Box(MARGIN + BTN_W + MARGIN, row_y, transcribe_x - BTN_W - 3 * MARGIN, BTN_H);
    selection_info->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    selection_info->labelcolor(DIM);

    transcribe_btn = new Fl_Button(transcribe_x, row_y, TRANSCRIBE_W, BTN_H);
    transcribe_btn->box(FL_ROUND_UP_BOX);
    transcribe_btn->clear_visible_focus();
    transcribe_btn->shortcut(FL_Enter);
    transcribe_btn->tooltip("Turn the selection into text (Enter)");
    transcribe_btn->callback(on_transcribe, this);

    // Fills the rest of the window
    const int transcript_y = row_y + BTN_H + MARGIN;
    transcript = new SmoothScroll(MARGIN, transcript_y, inner_w, win.h() - transcript_y - MARGIN);
    // Blends into the window, like the main window's results
    transcript->box(FL_FLAT_BOX);
    transcript->color(FL_BACKGROUND_COLOR);
    transcript_text = new WrappedText(MARGIN, transcript_y, transcript->content_w());
    transcript_text->color(FL_BACKGROUND_COLOR);
    transcript->end();

    refresh_transcribe_button();

    // No resizable() on purpose: FLTK then keeps the window at a fixed
    // size, which also makes tiling window managers float it
    win.end();
    win.callback(on_close, this);
}

CaptureWindow::~CaptureWindow(){
    Fl::remove_timeout(on_play_tick, this);
    // jobs then goes with the members, stopping and joining each thread.
    // Their results are never handled: Fl::run() has returned by now.
}

void CaptureWindow::open(std::vector<float> new_audio, int new_rate, int selection_seconds){
    stop_playing();
    // Its text would be of the capture there was
    cancel_transcription();

    audio = std::move(new_audio);
    rate = new_rate;
    waveform->audio(audio, rate);
    const size_t selected = std::min(audio.size(), static_cast<size_t>(selection_seconds) * static_cast<size_t>(rate));
    waveform->selection(audio.size() - selected, audio.size());
    refresh_selection();
    show_note("Press Enter to turn the selection into text, or Space to hear it first.");

    prepare_model();

    // Wayland doesn't let an app raise a window that's already shown, but
    // a window that's shown anew comes up in front, with the focus
    if (win.shown()) win.hide();
    win.show();
}

void CaptureWindow::refresh_selection(){
    const auto [begin, end] = waveform->selection();
    if (begin == end){
        selection_info->copy_label("Drag on the waveform to select a line.");
        play_btn->deactivate();
    } else {
        selection_info->copy_label(std::format("{:.1f} s selected", static_cast<double>(end - begin) / rate).c_str());
        play_btn->activate();
    }
    selection_info->redraw();
    refresh_transcribe_button();
}

void CaptureWindow::refresh_transcribe_button(){
    const auto [begin, end] = waveform->selection();
    transcribe_btn->label(transcribing ? "Transcribing..." : "Transcribe");
    if (transcribing || begin == end) transcribe_btn->deactivate();
    else transcribe_btn->activate();
}

void CaptureWindow::on_play(Fl_Widget*, void* data){
    CaptureWindow* self = static_cast<CaptureWindow*>(data);
    if (self->player && self->player->playing()) self->stop_playing();
    else self->play();
}

void CaptureWindow::play(){
    const auto [begin, end] = waveform->selection();
    if (begin == end) return;
    try {
        if (!player) player = std::make_unique<ClipPlayer>();
        player->play(std::span(audio).subspan(begin, end - begin), rate);
    } catch (const std::exception& e){
        fl_alert("%s", e.what());
        return;
    }
    play_btn->label("Stop");
    play_btn->tooltip("Stop playing (Space)");
    Fl::add_timeout(PLAY_TICK, on_play_tick, this);
}

void CaptureWindow::stop_playing(){
    Fl::remove_timeout(on_play_tick, this);
    if (player) player->stop();
    waveform->hide_playhead();
    play_btn->label("Play");
    play_btn->tooltip("Play the selection (Space)");
}

void CaptureWindow::on_play_tick(void* data){
    CaptureWindow* self = static_cast<CaptureWindow*>(data);
    if (!self->player->playing()){
        self->stop_playing();
        return;
    }
    self->waveform->playhead(self->waveform->selection().first + self->player->position());
    Fl::repeat_timeout(PLAY_TICK, on_play_tick, data);
}

void CaptureWindow::prepare_model(){
    // A job still running loads it anyway, or already has
    if (!jobs.empty()) return;
    const WhisperModel* model = find_model(settings.get().speech.whisper_model);
    // Said when Enter is pressed: maybe they only want to listen
    if (!model || !is_installed(*model)) return;

    const unsigned id = next_job_id++;
    transcriber.hold();
    // model points into WHISPER_MODELS, which lives for the whole program
    jobs.push_back({id, std::jthread([this, id, model](std::stop_token stop){
        auto* result = new JobResult{.win = this, .id = id};
        try {
            transcriber.prepare(*model, stop);
        } catch (const TranscriptionCancelled&){
            result->cancelled = true;
        } catch (const std::exception& e){
            // Said if it's still so when transcribing, which tries again
            result->error = e.what();
        }
        Fl::awake(on_job_done, result);
    })});
}

void CaptureWindow::on_transcribe(Fl_Widget*, void* data){
    static_cast<CaptureWindow*>(data)->transcribe();
}

void CaptureWindow::transcribe(){
    const auto [begin, end] = waveform->selection();
    if (transcribing || begin == end) return;

    const WhisperModel* model = find_model(settings.get().speech.whisper_model);
    if (!model || !is_installed(*model)){
        // Modal: waits for OK before returning
        fl_alert("No whisper model is ready to turn speech into text.\n\n"
                 "Download one and choose it in Settings > Speech-to-Text.");
        return;
    }

    const unsigned id = next_job_id++;
    transcribing = id;
    refresh_transcribe_button();
    show_note("Transcribing...");

    // A copy for the thread: a new capture replaces audio
    std::vector<float> clip(audio.begin() + static_cast<std::ptrdiff_t>(begin),
                            audio.begin() + static_cast<std::ptrdiff_t>(end));
    transcriber.hold();
    // Waits its turn if the model is still loading
    jobs.push_back({id, std::jthread([this, id, model, clip = std::move(clip), clip_rate = rate](std::stop_token stop){
        auto* result = new JobResult{.win = this, .id = id};
        try {
            const std::vector<float> samples = resample(clip, clip_rate, Transcriber::SAMPLE_RATE);
            result->text = transcriber.transcribe(*model, samples, "ja", stop);
        } catch (const TranscriptionCancelled&){
            result->cancelled = true;
        } catch (const std::exception& e){
            result->error = e.what();
        }
        Fl::awake(on_job_done, result);
    })});
}

void CaptureWindow::cancel_transcription(){
    if (!transcribing) return;
    // Its result still comes, to join the thread, but isn't shown
    for (Job& job : jobs)
        if (job.id == transcribing) job.thread.request_stop();
    transcribing = 0;
    refresh_transcribe_button();
}

void CaptureWindow::on_job_done(void* data){
    // unique_ptr so result is deleted when this returns
    std::unique_ptr<JobResult> result(static_cast<JobResult*>(data));
    CaptureWindow* self = result->win;

    // Its last act was queueing this, so joining takes no time
    const auto job = std::ranges::find(self->jobs, result->id, &Job::id);
    if (job != self->jobs.end()){
        job->thread.join();
        self->jobs.erase(job);
    }
    self->transcriber.release();

    // Loading the model, or cancelled
    if (result->id != self->transcribing) return;
    self->transcribing = 0;
    self->refresh_transcribe_button();

    if (!result->error.empty()) self->show_note(std::format("Couldn't turn the speech into text: {}", result->error));
    else if (result->text.empty()) self->show_note("No speech found in the selection.");
    else self->show_transcript({{result->text, TRANSCRIPT}});
}

void CaptureWindow::show_transcript(const std::vector<TextSpan>& spans){
    transcript->scroll_to_top();
    transcript_text->text(spans);
    // Its scrollbar depends on the new text's height
    transcript->redraw();
}

void CaptureWindow::show_note(const std::string& note){
    show_transcript({{note, NOTE}});
}

void CaptureWindow::on_close(Fl_Widget*, void* data){
    CaptureWindow* self = static_cast<CaptureWindow*>(data);
    // Escape while transcribing only cancels that
    if (self->transcribing && Fl::event() == FL_SHORTCUT && Fl::event_key() == FL_Escape){
        self->cancel_transcription();
        self->show_note("Cancelled. Press Enter to try again.");
        return;
    }

    self->stop_playing();
    self->cancel_transcription();
    self->win.hide();
    // Not kept any longer than it's needed
    self->waveform->audio({}, self->rate);
    self->audio.clear();
    self->audio.shrink_to_fit();
    self->show_transcript({});
}
