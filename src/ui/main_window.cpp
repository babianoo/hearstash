#include "ui/main_window.h"

#include <exception>
#include <format>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

// FLTK
#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_SVG_Image.H>

#include "core/deepl.h"
#include "core/recorder.h"
#include "core/settings.h"
#include "core/whisper_models.h"
#include "ui/clipboard.h"
#include "ui/history_store.h"
#include "ui/history_window.h"
#include "ui/icons.h"
#include "ui/jisho_format.h"
#include "ui/search_bar.h"
#include "ui/settings_store.h"
#include "ui/settings_window.h"
#include "ui/shared_transcriber.h"
#include "ui/theme.h"
#include "ui/smooth_scroll.h"
#include "ui/wrapped_text.h"

// Space around the search bar and results box
static constexpr int MARGIN = 10;
// Between the round buttons in the top-right corner
static constexpr int ICON_BTN_GAP = 6;
// Seconds between checks on a recording
static constexpr double RECORD_TICK = 0.1;

// A round button with an icon, like Settings, at x along the top. As tall
// as the search bar, so it lines up with it once the bar moves to the top.
static Fl_Button* icon_button(int x, const char* svg, const char* tooltip){
    Fl_Button* btn = new Fl_Button(x, MARGIN, PILL_H, PILL_H);
    // Same box as the Search button; on a square button it's a circle.
    // Pressed it becomes FL_ROUND_DOWN_BOX, which has the same shape.
    // (FL_ROUNDED_BOX's pressed look is a square, whose corners stay
    // behind on release.)
    btn->box(FL_ROUND_UP_BOX);
    // The button only borrows the image and never deletes it. It's needed
    // for as long as the window exists, so it's simply never freed.
    auto* icon = new Fl_SVG_Image(nullptr, svg);
    icon->scale(18, 18);
    btn->image(icon);
    btn->tooltip(tooltip);
    btn->clear_visible_focus();    // No dotted focus ring after a click
    return btn;
}

MainWindow::MainWindow(SettingsStore& settings, HistoryStore& history, ModelDownloader& downloader,
                       Capturer& capturer, SharedTranscriber& transcriber)
    : settings(settings), history(history), downloader(downloader), capturer(capturer), transcriber(transcriber){
    // win is constructed (as a member) before this body runs, so it's
    // the current group here: widgets created below are added to it.

    // Centered, a bit above the middle
    const int bar_w = 600;
    const int bar_x = (win.w() - bar_w) / 2, bar_y = win.h() * 2 / 5;
    search_bar = new SearchBar(bar_x, bar_y, bar_w);
    search_bar->callback(on_search, this);

    // Fills the space under the bar, same width
    const int results_y = bar_y + search_bar->h() + MARGIN;
    results = new SmoothScroll(bar_x, results_y, bar_w, win.h() - results_y - MARGIN);
    // Blends into the window: no border, window's color. Still a flat box,
    // not FL_NO_BOX, since it has to repaint what scrolling uncovers.
    results->box(FL_FLAT_BOX);
    results->color(FL_BACKGROUND_COLOR);
    // Inside the box's border, and stops before the scrollbar
    results_text = new WrappedText(bar_x + Fl::box_dx(results->box()),
                                   results_y + Fl::box_dy(results->box()),
                                   results->content_w());
    results_text->color(FL_BACKGROUND_COLOR);
    results->end();
    results->hide();    // Shown by move_bar_to_top() on the first search
    // Long text makes the bar taller; the results start under it
    search_bar->on_height_change([this]{ place_results(); });
    // Only the scrollbar under the mouse shows, so the bar's and the
    // results' don't look like two of the same
    search_bar->on_input_hover([this](bool hovered){ results->show_scrollbar(!hovered); });
    // Record Input in the "+" menu, and the red "+" that stops it
    search_bar->on_record([this]{ on_record(); });
    // Starts with the API picked last time, and remembers the one picked
    search_bar->set_api(settings.get().api.last_api);
    search_bar->on_api_change([this]{ remember_api(); });

    // Settings button in the top-right corner, and History left of it
    const int settings_x = win.w() - MARGIN - 25;
    icon_button(settings_x, ICON_SETTINGS, "Settings")->callback(on_settings, this);
    icon_button(settings_x - ICON_BTN_GAP - PILL_H, ICON_HISTORY, "History")->callback(on_history, this);

    // No resizable() on purpose: FLTK then keeps the window at a fixed size.
    win.end();
    win.callback(on_close, this);
}

MainWindow::~MainWindow(){
    Fl::remove_timeout(on_record_tick, this);
    // The members then go in reverse order: transcription_thread is
    // stopped and joined, and recorder stops a recording still running
}

void MainWindow::on_record(){
    switch (search_bar->record_state()){
    case RecordState::Idle: start_recording(); break;
    case RecordState::Recording: stop_recording(); break;
    // The "+" button is greyed out meanwhile
    case RecordState::Transcribing: break;
    }
}

void MainWindow::start_recording(){
    // Checked first, so nobody talks for nothing
    const WhisperModel* model = find_model(settings.get().speech.whisper_model);
    if (!model || !is_installed(*model)){
        // Modal: waits for an answer before returning
        if (fl_choice("No whisper model is ready to turn speech into text.\n\n"
                      "Download one and choose it in Settings > Speech-to-Text.",
                      "Cancel", "Open Settings", nullptr) == 1)
            settings_window().show_speech_to_text();
        return;
    }

    try {
        if (!recorder) recorder = std::make_unique<Recorder>();
        recorder->start(settings.get().speech.input_device);
    } catch (const std::exception& e){
        fl_alert("%s", e.what());
        return;
    }

    search_bar->record_state(RecordState::Recording);
    Fl::add_timeout(RECORD_TICK, on_record_tick, this);

    // The thread uses the model from now on
    transcriber.hold();
    // Loading the model alone takes seconds, so it starts now, while they
    // talk, and not on the UI thread. The thread then waits for the
    // recording, and only hands its result back through Fl::awake(). The
    // previous one has already ended: the state was Idle again only after
    // its result came in. model points into WHISPER_MODELS, which lives
    // for the whole program.
    const bool gpu = settings.get().speech.use_gpu;
    transcription_thread = std::jthread([this, model, gpu](std::stop_token stop){
        try {
            transcriber.prepare(*model, gpu, stop);
        } catch (const std::exception&){
            // Reported once there's a recording: transcribe() tries again.
            // Stopped, the wait below returns at once.
        }

        std::vector<float> samples;
        {
            std::unique_lock lock(recorded_mutex);
            // False if stopped first: the window is closing, and nobody's
            // waiting for the result
            if (!recorded_ready.wait(lock, stop, [this]{ return recorded.has_value(); })) return;
            samples = std::move(*recorded);
            recorded.reset();
        }

        auto* result = new Transcription{.win = this, .text = {}};
        try {
            // Nothing recorded is nothing said, even if the model failed
            if (!samples.empty()) result->text = transcriber.transcribe(*model, gpu, samples, "ja", stop);
        } catch (const TranscriptionCancelled&){
            delete result;
            return;
        } catch (const std::exception& e){
            result->error = e.what();
        }
        Fl::awake(on_transcribed, result);
    });
}

void MainWindow::on_record_tick(void* data){
    MainWindow* self = static_cast<MainWindow*>(data);
    if (self->recorder->full()){
        self->stop_recording();
        return;
    }
    self->search_bar->record_time(static_cast<int>(self->recorder->seconds()));
    Fl::repeat_timeout(RECORD_TICK, on_record_tick, data);
}

void MainWindow::stop_recording(){
    Fl::remove_timeout(on_record_tick, this);
    std::vector<float> samples = recorder->stop();
    // Even if it's empty, so the thread ends. It's Transcribing until then,
    // which with nothing recorded is only as long as the model takes to
    // finish loading.
    search_bar->record_state(RecordState::Transcribing);
    {
        std::lock_guard lock(recorded_mutex);
        recorded = std::move(samples);
    }
    recorded_ready.notify_one();
}

void MainWindow::on_transcribed(void* data){
    // unique_ptr so result is deleted when this returns
    std::unique_ptr<Transcription> result(static_cast<Transcription*>(data));
    MainWindow* self = result->win;
    // Its last act was queueing this, so joining takes no time
    self->transcription_thread.join();
    self->search_bar->record_state(RecordState::Idle);
    self->transcriber.release();

    if (!result->error.empty()) fl_alert("Couldn't turn the speech into text:\n\n%s", result->error.c_str());
    // In the box to check before searching: whisper gets words wrong,
    // the small models especially
    else if (!result->text.empty()) self->search_bar->set_text(result->text);
}

SettingsWindow& MainWindow::settings_window(){
    if (!settings_win) settings_win = std::make_unique<SettingsWindow>(settings, history, downloader, capturer);
    return *settings_win;
}

HistoryWindow& MainWindow::history_window(){
    if (!history_win) history_win = std::make_unique<HistoryWindow>(history, [this](const std::string& query, Api api){
            search_again(query, api);
        });
    return *history_win;
}

void MainWindow::show(int argc, char** argv){
    win.show(argc, argv);
    clipboard_init();    // Needs the display, which show() opens
}

void MainWindow::on_search(Fl_Widget*, void* data){
    static_cast<MainWindow*>(data)->search();
}

void MainWindow::search_again(const std::string& query, Api api){
    search_bar->set_text(query);
    search_bar->set_api(api);
    remember_api();
    search();
    // In front of the history window, so the result can be seen. Wayland
    // doesn't let an app raise its own window, so there it stays behind.
    win.show();
}

void MainWindow::remember_api(){
    const Api api = search_bar->api();
    // update() saves the file, so not when nothing changed
    if (settings.get().api.last_api != api) settings.update([api](Settings& s){ s.api.last_api = api; });
}

void MainWindow::search(){
    // Without the spaces and line breaks around it, so "兄弟 " and "兄弟"
    // are one search in the history
    std::string keyword = search_bar->text();
    keyword.erase(keyword.find_last_not_of(" \t\r\n") + 1);
    keyword.erase(0, keyword.find_first_not_of(" \t\r\n"));
    if (keyword.empty()) return;

    const Api api = search_bar->api();

    // Translated before: shown from the history, without asking DeepL,
    // so it doesn't use up the monthly quota. Works without a key too.
    const std::string* cached = api == Api::DeepL ? history.cached_translation(keyword) : nullptr;

    // Checked before anything changes on screen: without a key there's no
    // search at all. Copied for the search thread, since the settings may
    // change while it runs.
    std::string deepl_key;
    if (api == Api::DeepL && !cached){
        deepl_key = effective_deepl_key(settings.get());
        if (deepl_key.empty()){
            // Modal: waits for OK before returning
            fl_alert("No DeepL API key found.\n\n"
                     "Input the DeepL key in the settings or "
                     "set the DEEPL_API_KEY environment variable to use DeepL.");
            return;
        }
    }

    // Down to one line, so a long text doesn't take space from the results
    search_bar->collapse();
    move_bar_to_top();

    if (api == Api::MyMemory){
        show_text("MyMemory isn't connected yet.");
        return;
    }

    const unsigned id = ++latest_search;

    if (cached){
        // Copied: adding the search to the history replaces the entry
        // cached points into
        show_result({.win = this, .id = id, .keyword = keyword, .api = api, .translation = *cached});
        return;
    }

    // Not the keyword itself: a long paragraph would fill the results area
    show_text(api == Api::DeepL ? "Translating..." : "Searching...");

    // The request can take seconds, and FLTK only redraws between
    // callbacks, so doing it here would freeze the window. The thread gets
    // copies of what it needs; it never touches a widget itself.
    std::thread([this, id, api, keyword = std::move(keyword), deepl_key = std::move(deepl_key)]{
        auto* result = new SearchResult{.win = this, .id = id, .keyword = keyword, .api = api};
        try {
            if (api == Api::DeepL) result->translation = deepl_translate(keyword, deepl_key);
            else result->entries = jisho_search(keyword);
        } catch (const std::exception& e){
            result->error = e.what();
        }

        // Queues on_search_done to run on the UI thread, which then owns
        // result. Widgets may only be changed from there.
        Fl::awake(on_search_done, result);
    }).detach();    // Nothing waits for it; it ends on its own after awake()
}

void MainWindow::on_close(Fl_Widget*, void*){
    // FLTK calls it for Escape too, which shouldn't quit the app
    if (Fl::event() == FL_SHORTCUT && Fl::event_key() == FL_Escape) return;
    // Settings too: Fl::run() only returns once no window is shown, and
    // until then a recording would go on with nothing on screen. The
    // destructors then stop it, and cancel any download.
    while (Fl::first_window()) Fl::first_window()->hide();
}

void MainWindow::on_settings(Fl_Widget*, void* data){
    static_cast<MainWindow*>(data)->settings_window().show();
}

void MainWindow::on_history(Fl_Widget*, void* data){
    static_cast<MainWindow*>(data)->history_window().show();
}

void MainWindow::on_search_done(void* data){
    // unique_ptr so result is deleted when this returns
    std::unique_ptr<SearchResult> result(static_cast<SearchResult*>(data));
    result->win->show_result(*result);
}

// Like Google: the bar starts in the middle and jumps to the top on the
// first search, and the results box appears in the space under it.
void MainWindow::move_bar_to_top(){
    const int top = MARGIN;
    if (search_bar->y() == top) return;

    // Moving a group moves its children (input, buttons) along with it
    search_bar->position(search_bar->x(), top);
    place_results();
    results->show();
    win.redraw();
}

void MainWindow::place_results(){
    const int results_y = search_bar->y() + search_bar->h() + MARGIN;
    results->resize(results->x(), results_y, results->w(), win.h() - results_y - MARGIN);
}

void MainWindow::show_result(const SearchResult& result){
    // A newer search started since this one; its result will come later
    if (result.id != latest_search) return;

    if (!result.error.empty()){
        show_text(std::format("Search failed: {}", result.error));
    } else if (result.translation){
        show_text(*result.translation);
        history.add(result.keyword, result.api, result.translation);
    } else if (result.entries.empty()){
        // Not in the history: most likely a typo, and nothing to look at
        // again
        show_text(std::format("No results for {}.", result.keyword));
    } else {
        show_text(format_entries(result.entries));
        history.add(result.keyword, result.api);
    }
}

void MainWindow::show_text(std::string_view text){
    show_text(std::vector<TextSpan>{{std::string(text)}});
}

void MainWindow::show_text(const std::vector<TextSpan>& spans){
    results->scroll_to_top();
    results_text->text(spans);
    // Its scrollbar depends on the new text's height
    results->redraw();
}
