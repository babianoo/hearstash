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
#include "core/lookup_text.h"
#include "core/resample.h"
#include "core/settings.h"
#include "core/transcriber.h"
#include "core/whisper_models.h"
#include "ui/card_window.h"
#include "ui/jisho_format.h"
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
static constexpr int ANKI_W = 120;
// The transcript's width; the lookup panel takes the rest
static constexpr int TRANSCRIPT_W = 360;
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
    const int anki_x = transcribe_x - MARGIN - ANKI_W;
    selection_info = new Fl_Box(MARGIN + BTN_W + MARGIN, row_y, anki_x - BTN_W - 3 * MARGIN, BTN_H);
    selection_info->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    selection_info->labelcolor(DIM);

    transcribe_btn = new Fl_Button(transcribe_x, row_y, TRANSCRIBE_W, BTN_H);
    transcribe_btn->box(FL_ROUND_UP_BOX);
    transcribe_btn->clear_visible_focus();
    transcribe_btn->shortcut(FL_Enter);
    transcribe_btn->tooltip("Turn the selection into text (Enter)");
    transcribe_btn->callback(on_transcribe, this);

    anki_btn = new Fl_Button(anki_x, row_y, ANKI_W, BTN_H, "Add to Anki");
    anki_btn->box(FL_ROUND_UP_BOX);
    anki_btn->clear_visible_focus();
    anki_btn->tooltip("Make a card of the word looked up and this line (Ctrl+N)");
    anki_btn->shortcut(FL_CTRL | 'n');
    anki_btn->callback(on_add_to_anki, this);

    // The rest of the window: the transcript on the left, then a line, then
    // the lookup panel. Both blend into the window, like the main window's
    // results.
    const int panel_y = row_y + BTN_H + MARGIN, panel_h = win.h() - panel_y - MARGIN;
    transcript = new SmoothScroll(MARGIN, panel_y, TRANSCRIPT_W, panel_h);
    transcript->box(FL_FLAT_BOX);
    transcript->color(FL_BACKGROUND_COLOR);
    transcript_text = new WrappedText(MARGIN, panel_y, transcript->content_w());
    transcript_text->color(FL_BACKGROUND_COLOR);
    transcript_text->on_select([this]{ look_up_selection(); });
    transcript->end();

    const int line_x = MARGIN + TRANSCRIPT_W + MARGIN;
    Fl_Box* line = new Fl_Box(line_x, panel_y, 1, panel_h);
    line->box(FL_FLAT_BOX);
    line->color(sidebar_color());

    const int dictionary_x = line_x + 1 + MARGIN;
    dictionary = new SmoothScroll(dictionary_x, panel_y, MARGIN + inner_w - dictionary_x, panel_h);
    dictionary->box(FL_FLAT_BOX);
    dictionary->color(FL_BACKGROUND_COLOR);
    dictionary_text = new WrappedText(dictionary_x, panel_y, dictionary->content_w());
    dictionary_text->color(FL_BACKGROUND_COLOR);
    dictionary->end();

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

void CaptureWindow::open(std::vector<float> new_audio, int new_rate, int selection_seconds, Image new_screenshot,
                         std::string new_screenshot_error){
    stop_playing();
    // Its text would be of the capture there was
    cancel_transcription();
    screenshot = std::move(new_screenshot);
    screenshot_error = std::move(new_screenshot_error);
    transcript_shown.clear();
    entry_shown.reset();

    audio = std::move(new_audio);
    rate = new_rate;
    waveform->audio(audio, rate);
    const size_t selected = std::min(audio.size(), static_cast<size_t>(selection_seconds) * static_cast<size_t>(rate));
    waveform->selection(audio.size() - selected, audio.size());
    refresh_selection();
    show_note("Press Enter to turn the selection into text, or Space to hear it first.");
    // A lookup still running is of the text there was
    ++latest_lookup;
    show_lookup({});

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
    // A card needs something to show on its front
    if (transcript_shown.empty() || begin == end) anki_btn->deactivate();
    else anki_btn->activate();
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
    transcript_shown.clear();
    entry_shown.reset();
    ++latest_lookup;
    show_lookup({});
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
    else {
        self->show_transcript({{result->text, TRANSCRIPT}});
        self->transcript_shown = result->text;
        self->entry_shown.reset();
        ++self->latest_lookup;
        self->show_lookup({{"Select a word in the text to look it up.", NOTE}});
    }
    self->refresh_transcribe_button();
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

void CaptureWindow::look_up_selection(){
    const std::string keyword = lookup_text(transcript_text->selected_text());
    if (keyword.empty()) return;

    const unsigned id = ++latest_lookup;
    show_lookup({{std::format("Looking up {}...", keyword), NOTE}});

    // Like the main window's searches: the request can take seconds, so
    // it's made on a thread that only hands its result back. Nothing
    // waits for it.
    std::thread([this, id, keyword]{
        auto* result = new LookupResult{.win = this, .id = id, .keyword = keyword};
        try {
            result->entries = jisho_search(keyword);
        } catch (const std::exception& e){
            result->error = e.what();
        }
        Fl::awake(on_lookup_done, result);
    }).detach();
}

void CaptureWindow::on_lookup_done(void* data){
    // unique_ptr so result is deleted when this returns
    std::unique_ptr<LookupResult> result(static_cast<LookupResult*>(data));
    CaptureWindow* self = result->win;
    // A newer lookup started since, or the text changed
    if (result->id != self->latest_lookup) return;

    self->keyword_shown = result->keyword;
    if (result->entries.empty()) self->entry_shown.reset();
    else self->entry_shown.emplace(result->entries.front());

    if (!result->error.empty())
        self->show_lookup({{std::format("Couldn't look up {}: {}", result->keyword, result->error), NOTE}});
    else if (result->entries.empty())
        // Jisho knows dictionary forms, and only some inflected ones
        self->show_lookup({{std::format("Nothing found for {}. Try selecting less, or the word's dictionary form.",
                                        result->keyword), NOTE}});
    else self->show_lookup(format_entries(result->entries));
}

void CaptureWindow::show_lookup(const std::vector<TextSpan>& spans){
    dictionary->scroll_to_top();
    dictionary_text->text(spans);
    dictionary->redraw();
}

void CaptureWindow::on_add_to_anki(Fl_Widget*, void* data){
    static_cast<CaptureWindow*>(data)->add_to_anki();
}

// A dictionary entry's meanings, one numbered line each
static std::string meanings(const JishoEntry& entry){
    std::string text;
    int n = 1;
    for (const JishoSense& sense : entry.senses){
        if (!text.empty()) text += '\n';
        text += std::format("{}. ", n++);
        for (size_t i = 0; i < sense.definitions.size(); ++i){
            if (i > 0) text += "; ";
            text += sense.definitions[i];
        }
    }
    return text;
}

void CaptureWindow::add_to_anki(){
    const auto [begin, end] = waveform->selection();
    if (transcript_shown.empty() || begin == end) return;
    stop_playing();

    CardDraft draft;
    draft.sentence = transcript_shown;
    if (entry_shown && !entry_shown->forms.empty()){
        // Kana-only words have just the reading, which then is the word
        const JishoForm& form = entry_shown->forms.front();
        draft.word = form.word.empty() ? form.reading : form.word;
        draft.reading = form.reading;
        draft.meaning = meanings(*entry_shown);
    }
    draft.audio.assign(audio.begin() + static_cast<std::ptrdiff_t>(begin), audio.begin() + static_cast<std::ptrdiff_t>(end));
    draft.rate = rate;
    // A copy: more than one card can be made from a capture
    draft.screenshot = screenshot;
    draft.screenshot_error = screenshot_error;

    if (!card_win) card_win = std::make_unique<CardWindow>(settings);
    card_win->open(std::move(draft));
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
    self->screenshot = {};
    self->transcript_shown.clear();
    self->entry_shown.reset();
    self->show_transcript({});
    ++self->latest_lookup;
    self->show_lookup({});
}
