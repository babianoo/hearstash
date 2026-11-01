#include "ui/settings_window.h"

#include <algorithm>
#include <exception>
#include <format>
#include <iterator>
#include <string>

// FLTK
#include <FL/fl_ask.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Round_Button.H>
#include <FL/Fl_SVG_Image.H>

#include "core/anki_connect.h"
#include "core/recorder.h"
#include "core/settings.h"
#include "core/whisper_models.h"
#include "ui/capturer.h"
#include "ui/category_button.h"
#include "ui/clipboard_input.h"
#include "ui/global_hotkey.h"
#include "ui/history_store.h"
#include "ui/icons.h"
#include "ui/model_downloader.h"
#include "ui/settings_store.h"
#include "ui/theme.h"

// Same categories as japanese-helper, in sidebar order, then the rolling
// buffer's and Anki's
static constexpr const char* CATEGORY_NAMES[] = {"General", "History", "API", "Speech-to-Text", "Buffer", "Anki"};
// Indexes into CATEGORY_NAMES
enum Category : size_t { GENERAL, HISTORY, API, SPEECH_TO_TEXT, BUFFER, ANKI };

static constexpr int SIDEBAR_W = 160;
static constexpr int CATEGORY_H = 36;
// Space around the sidebar's list and a page's contents
static constexpr int MARGIN = 20;

static const Fl_Color ERROR_COLOR = fl_rgb_color(200, 40, 40);

// A row on the Speech-to-Text page, and its button
static constexpr int MODEL_ROW_H = 34;
static constexpr int MODEL_BTN_W = 80, MODEL_BTN_H = 26;
// The size or progress column, left of the button
static constexpr int MODEL_STATUS_W = 110;

// On the History page: the box for how many searches to keep, wide
// enough for 1000, and the button that clears them
static constexpr int CAPACITY_W = 80;
static constexpr int CLEAR_BTN_W = 120;

// Replaces choice's items with labels, and selects item selected
static void set_items(Fl_Choice& choice, const std::vector<std::string>& labels, int selected){
    choice.clear();
    for (size_t i = 0; i < labels.size(); ++i){
        // add() reads '/', '&', '\' and more in a label as menu syntax,
        // and a device or deck name can have them. So each is added as a
        // unique placeholder (add() replaces an item with the same label),
        // then replace() sets the text as it is. Only '&' still means
        // something when drawn, underlining the next letter, so it's
        // doubled.
        const int index = choice.add(std::format("item{}", i).c_str());
        std::string text;
        for (char c : labels[i]){
            text += c;
            if (c == '&') text += '&';
        }
        choice.replace(index, text.c_str());
    }
    choice.value(selected);
    choice.redraw();
}

// Where model is in WHISPER_MODELS, and so in model_rows
static size_t index_of(const WhisperModel& model){
    return static_cast<size_t>(&model - WHISPER_MODELS.data());
}

SettingsWindow::SettingsWindow(SettingsStore& settings, HistoryStore& history, ModelDownloader& downloader,
                               Capturer& capturer)
    : settings(settings), history(history), downloader(downloader), capturer(capturer){
    // win is the current group here, like in MainWindow

    // Its own group so the radio buttons only deselect each other
    Fl_Group* sidebar = new Fl_Group(0, 0, SIDEBAR_W, win.h());
    sidebar->box(FL_FLAT_BOX);
    sidebar->color(sidebar_color());
    for (size_t i = 0; i < std::size(CATEGORY_NAMES); ++i){
        const int y = MARGIN + static_cast<int>(i) * CATEGORY_H;
        Fl_Button* category = new CategoryButton(0, y, SIDEBAR_W, CATEGORY_H, CATEGORY_NAMES[i], MARGIN);
        category->callback(on_category, this);
        categories.push_back(category);
    }
    sidebar->end();

    const int page_x = SIDEBAR_W, page_w = win.w() - SIDEBAR_W;
    for (size_t i = 0; i < std::size(CATEGORY_NAMES); ++i){
        Fl_Group* page = new Fl_Group(page_x, 0, page_w, win.h());
        if (i == HISTORY) add_history_settings(page_x, page_w);
        if (i == API) add_api_settings(page_x, page_w);
        if (i == SPEECH_TO_TEXT) add_speech_settings(page_x, page_w);
        if (i == BUFFER) add_buffer_settings(page_x, page_w);
        if (i == ANKI) add_anki_settings(page_x, page_w);

        page->end();
        page->hide();    // Shown by select()
        pages.push_back(page);
    }

    select(0);

    // No resizable() on purpose: FLTK then keeps the window at a fixed size.
    win.end();
    win.callback(on_close, this);

    // While the window is closed too, so a download that finishes then
    // still becomes the chosen model if there's none
    downloader_listener = downloader.add_listener([this](const WhisperModel& model){ refresh_model(model); });
    history_listener = history.add_listener([this]{ refresh_history(); });
    refresh_history();
    // After the Capturer's own listener, which applies the change first
    settings_listener = settings.add_listener([this]{ refresh_buffer(); });
}

SettingsWindow::~SettingsWindow(){
    // E.g. typed, then the app quit from the main window. The stores
    // outlive the window, and save it after.
    apply_capacity();
    apply_buffer_seconds();
    history.remove_listener(history_listener);
    downloader.remove_listener(downloader_listener);
    settings.remove_listener(settings_listener);
}

// How many searches to keep: a label, the number, and a note on what
// lowering it does. Under them the button that clears the history, with
// how many searches there are next to it.
void SettingsWindow::add_history_settings(int x, int w){
    const int left = x + MARGIN, right = x + w - MARGIN;
    // Lines up with the first category in the sidebar, like the API page
    int y = MARGIN + (CATEGORY_H - 20) / 2;

    Fl_Box* label = new Fl_Box(left, y, right - left, 20, "Searches to keep");
    label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += label->h() + 4;

    capacity_input = new ClipboardInput(left, y, CAPACITY_W, PILL_H);
    capacity_input->type(FL_INT_INPUT);
    capacity_input->value(settings.get().history.capacity);
    // Not on every keystroke: typing 150 goes through 1, which would
    // delete all but the newest 10 searches. Only on Enter or leaving the
    // box, and when the page or window is left: see apply_capacity().
    capacity_input->when(FL_WHEN_ENTER_KEY | FL_WHEN_RELEASE);
    capacity_input->callback(on_capacity, this);
    y += PILL_H + 6;

    Fl_Box* note = new Fl_Box(left, y, right - left, 20);
    note->copy_label(std::format("From {} to {}. Lowering it deletes the oldest searches over it.",
                                 HistorySettings::MIN_CAPACITY, HistorySettings::MAX_CAPACITY).c_str());
    note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    note->labelsize(12);
    note->labelcolor(DIM);
    y += note->h() + 2 * MARGIN;

    clear_history_btn = new Fl_Button(left, y, CLEAR_BTN_W, MODEL_BTN_H, "Clear History");
    clear_history_btn->box(FL_ROUND_UP_BOX);
    clear_history_btn->clear_visible_focus();
    clear_history_btn->callback(on_clear_history, this);

    history_count = new Fl_Box(left + CLEAR_BTN_W + 10, y, right - left - CLEAR_BTN_W - 10, MODEL_BTN_H);
    history_count->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    history_count->labelcolor(DIM);
}

void SettingsWindow::refresh_history(){
    const size_t count = history.entries().size();
    if (count == 0) history_count->copy_label("The history is empty.");
    else history_count->copy_label(std::format("{} search{} in the history", count, count == 1 ? "" : "es").c_str());
    if (count == 0) clear_history_btn->deactivate();
    else clear_history_btn->activate();
}

void SettingsWindow::on_capacity(Fl_Widget*, void* data){
    static_cast<SettingsWindow*>(data)->apply_capacity();
}

void SettingsWindow::apply_capacity(){
    // Empty, or not a number: put back as it was
    int capacity = settings.get().history.capacity;
    try {
        capacity = std::stoi(capacity_input->value());
    } catch (const std::exception&){}

    if (capacity != settings.get().history.capacity){
        settings.update([capacity](Settings& s){ s.history.capacity = capacity; });
        history.set_capacity(static_cast<size_t>(settings.get().history.capacity));
    }
    // Clamped by update(), and shown that way
    capacity_input->value(settings.get().history.capacity);
}

void SettingsWindow::on_close(Fl_Widget*, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    // Typed but not applied yet: closing is done with it too
    self->apply_capacity();
    self->apply_buffer_seconds();
    self->win.hide();
}

void SettingsWindow::on_clear_history(Fl_Widget*, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    const size_t count = self->history.entries().size();
    const std::string question = std::format("Delete all {} search{} from the history?\n\nThis can't be undone.",
                                             count, count == 1 ? "" : "es");
    // Modal: waits for an answer before returning
    if (fl_choice("%s", "Cancel", "Delete", nullptr, question.c_str()) != 1) return;
    self->history.clear();
}

// DeepL key: a label, then the input with an eye button on its right,
// then a note on what an empty one means
void SettingsWindow::add_api_settings(int x, int w){
    const int left = x + MARGIN, right = x + w - MARGIN;
    // Lines up with the first category in the sidebar
    int y = MARGIN + (CATEGORY_H - 20) / 2;

    Fl_Box* label = new Fl_Box(left, y, right - left, 20, "DeepL API key");
    label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += label->h() + 4;

    // Same height as the search bar; the button is square, like the gear
    const int btn_x = right - PILL_H;
    key_input = new ClipboardInput(left, y, btn_x - 6 - left, PILL_H);
    key_input->value(settings.get().api.deepl_key.c_str());
    // On every keystroke, so the key is in use as soon as it's typed
    key_input->when(FL_WHEN_CHANGED);
    key_input->callback(on_key_changed, this);

    key_visibility_btn = new Fl_Button(btn_x, y, PILL_H, PILL_H);
    key_visibility_btn->box(FL_ROUND_UP_BOX);    // Round, like the gear
    key_visibility_btn->clear_visible_focus();   // No dotted focus ring after a click
    key_visibility_btn->callback(on_key_visibility, this);
    set_key_hidden(true);
    y += PILL_H + 6;

    Fl_Box* note = new Fl_Box(left, y, right - left, 20,
                              "If empty, the DEEPL_API_KEY environment variable is used.");
    note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    note->labelsize(12);
    note->labelcolor(DIM);
}

// Whisper models: a heading, a row per model, then a note. A row is a
// radio button labelled with the model's name, its size, and a button.
void SettingsWindow::add_speech_settings(int x, int w){
    const int left = x + MARGIN, right = x + w - MARGIN;
    // Lines up with the first category in the sidebar, like the API page
    int y = MARGIN + (CATEGORY_H - 20) / 2;

    Fl_Box* label = new Fl_Box(left, y, right - left, 20, "Whisper model");
    label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += label->h() + 4;

    const int btn_x = right - MODEL_BTN_W;
    const int status_x = btn_x - 8 - MODEL_STATUS_W;
    for (const WhisperModel& model : WHISPER_MODELS){
        ModelRow row;

        // Not in a group of their own: radio buttons only deselect the
        // ones in the same group, which is the page for all of them
        row.choose = new Fl_Round_Button(left, y, status_x - 8 - left, MODEL_ROW_H);
        row.choose->type(FL_RADIO_BUTTON);
        row.choose->copy_label(std::string(model.id).c_str());
        row.choose->clear_visible_focus();
        row.choose->callback(on_model_chosen, this);

        row.size = new Fl_Box(status_x, y, MODEL_STATUS_W, MODEL_ROW_H);
        row.size->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

        row.progress = new Fl_Progress(status_x, y + (MODEL_ROW_H - 18) / 2, MODEL_STATUS_W, 18);
        row.progress->minimum(0);
        row.progress->maximum(1);
        row.progress->selection_color(ACCENT);
        row.progress->labelsize(12);
        row.progress->hide();

        row.action = new Fl_Button(btn_x, y + (MODEL_ROW_H - MODEL_BTN_H) / 2, MODEL_BTN_W, MODEL_BTN_H);
        row.action->box(FL_ROUND_UP_BOX);
        row.action->clear_visible_focus();
        row.action->callback(on_model_action, this);

        model_rows.push_back(row);
        y += MODEL_ROW_H;
    }
    y += 6;

    Fl_Box* note = new Fl_Box(left, y, right - left, 20,
                              "Larger models are more accurate, but slower and use more memory.");
    note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    note->labelsize(12);
    note->labelcolor(DIM);
    y += note->h();

    // Not worth failing over: the folder is only shown, not needed here
    try {
        Fl_Box* folder = new Fl_Box(left, y, right - left, 20);
        const std::string dir = models_dir().string();
        folder->copy_label(std::format("Saved in {}", dir).c_str());
        // In full, in case it's cut off
        folder->copy_tooltip(dir.c_str());
        folder->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
        folder->labelsize(12);
        folder->labelcolor(DIM);
    } catch (const std::exception&){}
    y += 20 + 14;

    // Microphone: a label, then a dropdown as wide as the rows. Filled by
    // refresh_microphones(), since what's plugged in changes.
    Fl_Box* mic_label = new Fl_Box(left, y, right - left, 20, "Microphone");
    mic_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += mic_label->h() + 4;

    mic_choice = new Fl_Choice(left, y, right - left, PILL_H);
    mic_choice->clear_visible_focus();
    mic_choice->callback(on_mic_chosen, this);
}

void SettingsWindow::show(){
    // A model may have been deleted or added outside the app meanwhile
    refresh_models();
    refresh_microphones();
    refresh_buffer();
    // Decks may have been added in Anki meanwhile
    anki_listed = false;
    if (pages[ANKI]->visible()) refresh_anki();
    win.show();
}

void SettingsWindow::show_speech_to_text(){
    select(SPEECH_TO_TEXT);
    show();
}

void SettingsWindow::on_category(Fl_Widget* w, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    const auto it = std::ranges::find(self->categories, w);
    self->select(it - self->categories.begin());
}

void SettingsWindow::select(size_t i){
    // Leaving the History or Buffer page is done with it too. The box
    // keeps focus while hidden, so it wouldn't hear about it otherwise.
    apply_capacity();
    // Already done when the category was clicked, but not when it's
    // selected from code
    categories[i]->setonly();
    for (size_t p = 0; p < pages.size(); ++p){
        if (p == i) pages[p]->show();
        else pages[p]->hide();
    }
    // Asked when the page is first looked at, not every time the window
    // opens: it's only needed here
    if (i == ANKI && !anki_listed) refresh_anki();
}

void SettingsWindow::on_key_changed(Fl_Widget*, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    // As typed: update() trims it
    self->settings.update([self](Settings& s){ s.api.deepl_key = self->key_input->value(); });
}

void SettingsWindow::on_key_visibility(Fl_Widget*, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    self->set_key_hidden(self->key_input->input_type() != FL_SECRET_INPUT);
}

void SettingsWindow::set_key_hidden(bool hidden){
    // Made once and kept for the whole program; the button only borrows
    // them.
    static Fl_SVG_Image* show_icon = []{
        auto* icon = new Fl_SVG_Image(nullptr, ICON_SHOW);
        icon->scale(18, 18);
        return icon;
    }();
    static Fl_SVG_Image* hide_icon = []{
        auto* icon = new Fl_SVG_Image(nullptr, ICON_HIDE);
        icon->scale(18, 18);
        return icon;
    }();

    key_input->type(hidden ? FL_SECRET_INPUT : FL_NORMAL_INPUT);
    key_input->redraw();
    // The icon shows what clicking does
    key_visibility_btn->image(hidden ? show_icon : hide_icon);
    key_visibility_btn->tooltip(hidden ? "Show key" : "Hide key");
    key_visibility_btn->redraw();
}

void SettingsWindow::on_model_chosen(Fl_Widget* w, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    const WhisperModel& model = WHISPER_MODELS[self->model_row_of(w)];
    self->settings.update([&model](Settings& s){ s.speech.whisper_model = model.id; });
}

void SettingsWindow::on_model_action(Fl_Widget* w, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    const WhisperModel& model = WHISPER_MODELS[self->model_row_of(w)];

    if (self->downloader.state(model).status == ModelDownloader::Status::Downloading){
        self->downloader.cancel(model);
        // Until the download has stopped, which refreshes the row
        w->deactivate();
    } else if (is_installed(model)){
        self->delete_model(model);
    } else {
        // Download, or Retry after a failure
        self->downloader.start(model);
    }
}

size_t SettingsWindow::model_row_of(const Fl_Widget* w) const {
    const auto it = std::ranges::find_if(model_rows, [w](const ModelRow& row){
        return row.choose == w || row.action == w;
    });
    return static_cast<size_t>(it - model_rows.begin());
}

void SettingsWindow::refresh_model(const WhisperModel& model){
    ModelRow& row = model_rows[index_of(model)];
    const ModelDownloader::State& state = downloader.state(model);
    const bool downloading = state.status == ModelDownloader::Status::Downloading;
    const bool installed = is_installed(model);

    // A download that just finished becomes the chosen model if there's
    // none, so the first one works without another click
    if (row.downloading && !downloading && installed && settings.get().speech.whisper_model.empty())
        settings.update([&model](Settings& s){ s.speech.whisper_model = model.id; });
    row.downloading = downloading;

    // Only an installed model can be chosen. A chosen one that's gone,
    // e.g. deleted outside the app, stays selected but inactive.
    row.choose->value(settings.get().speech.whisper_model == model.id);
    if (installed) row.choose->activate();
    else row.choose->deactivate();

    row.action->activate();
    if (downloading){
        row.size->hide();
        row.progress->show();
        // The server sends the size with its first bytes
        if (state.total){
            row.progress->value(static_cast<float>(static_cast<double>(state.received) / static_cast<double>(state.total)));
            row.progress->copy_label(std::format("{}%", state.received * 100 / state.total).c_str());
        } else {
            row.progress->value(0);
            row.progress->copy_label("Starting...");
        }
        row.action->copy_label("Cancel");
    } else {
        row.progress->hide();
        row.size->show();
        if (state.status == ModelDownloader::Status::Failed){
            row.size->copy_label("Download failed");
            row.size->labelcolor(ERROR_COLOR);
            row.size->copy_tooltip(state.error.c_str());
            row.action->copy_label("Retry");
        } else {
            row.size->copy_label(std::string(model.size).c_str());
            row.size->labelcolor(DIM);
            row.size->tooltip(nullptr);
            row.action->copy_label(installed ? "Delete" : "Download");
        }
    }

    row.choose->redraw();
    row.size->redraw();
    row.progress->redraw();
    row.action->redraw();
}

void SettingsWindow::refresh_models(){
    for (const WhisperModel& model : WHISPER_MODELS) refresh_model(model);
}

void SettingsWindow::delete_model(const WhisperModel& model){
    // A large one takes minutes to download again
    const std::string question = std::format("Delete the {} model?\n\nUsing it again means downloading {} again.",
                                             model.id, model.size);
    // Modal: waits for an answer before returning. Escape is Cancel.
    if (fl_choice("%s", "Cancel", "Delete", nullptr, question.c_str()) != 1) return;

    try {
        remove_model(model);
    } catch (const std::exception& e){
        fl_alert("Couldn't delete the %s model:\n\n%s", std::string(model.id).c_str(), e.what());
    }

    // Nothing to transcribe with any more
    if (!is_installed(model) && settings.get().speech.whisper_model == model.id)
        settings.update([](Settings& s){ s.speech.whisper_model.clear(); });
    refresh_model(model);
}

void SettingsWindow::on_mic_chosen(Fl_Widget*, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    const int i = self->mic_choice->value();
    if (i < 0 || i >= static_cast<int>(self->mic_names.size())) return;
    const std::string name = self->mic_names[static_cast<size_t>(i)];
    self->settings.update([&name](Settings& s){ s.speech.input_device = name; });
}

void SettingsWindow::refresh_microphones(){
    const std::string saved = settings.get().speech.input_device;
    const std::vector<std::string> plugged_in = list_input_devices();

    mic_names = {""};
    mic_names.insert(mic_names.end(), plugged_in.begin(), plugged_in.end());
    // The saved one stays in the list while it's unplugged, so opening the
    // window then doesn't lose the choice. Recording uses the default
    // meanwhile.
    const bool saved_missing = !saved.empty() && std::ranges::find(plugged_in, saved) == plugged_in.end();
    if (saved_missing) mic_names.push_back(saved);

    std::vector<std::string> labels;
    for (size_t i = 0; i < mic_names.size(); ++i){
        std::string label = i == 0 ? "Default" : mic_names[i];
        if (saved_missing && i == mic_names.size() - 1) label += " (not plugged in)";
        labels.push_back(std::move(label));
    }
    const auto selected = std::ranges::find(mic_names, saved);
    set_items(*mic_choice, labels, static_cast<int>(selected - mic_names.begin()));
}

// A label over a box for a number of seconds, with a note right of the box
static ClipboardInput* seconds_input(int left, int& y, int w, const char* label_text, const std::string& note_text){
    Fl_Box* label = new Fl_Box(left, y, w, 20, label_text);
    label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += label->h() + 4;

    auto* input = new ClipboardInput(left, y, CAPACITY_W, PILL_H);
    input->type(FL_INT_INPUT);
    // Like the capacity, only on Enter or leaving the box: typing 60 goes
    // through 6, which would forget what was recorded twice
    input->when(FL_WHEN_ENTER_KEY | FL_WHEN_RELEASE);

    Fl_Box* note = new Fl_Box(left + CAPACITY_W + 10, y, w - CAPACITY_W - 10, PILL_H);
    note->copy_label(note_text.c_str());
    note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    note->labelsize(12);
    note->labelcolor(DIM);
    y += PILL_H + 14;
    return input;
}

// Whether it records, then how long it keeps and how much a capture
// starts with selected, then whether it's recording now
void SettingsWindow::add_buffer_settings(int x, int w){
    const int left = x + MARGIN, right = x + w - MARGIN;
    // Lines up with the first category in the sidebar, like the API page
    int y = MARGIN + (CATEGORY_H - 20) / 2;

    buffer_enabled = new Fl_Check_Button(left, y, right - left, 20,
                                         "Keep the last seconds of what the computer plays");
    buffer_enabled->value(settings.get().buffer.enabled);
    buffer_enabled->clear_visible_focus();
    buffer_enabled->callback(on_buffer_enabled, this);
    y += buffer_enabled->h() + 2;

    Fl_Box* privacy = new Fl_Box(left, y, right - left, 20,
                                 "It's only kept in memory. Nothing is saved unless you add it to a card.");
    privacy->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    privacy->labelsize(12);
    privacy->labelcolor(DIM);
    y += privacy->h() + 14;

    buffer_seconds_input = seconds_input(left, y, right - left, "Seconds to keep",
        std::format("From {} to {}. Changing it forgets what was kept.",
                    BufferSettings::MIN_SECONDS, BufferSettings::MAX_SECONDS));
    buffer_seconds_input->value(settings.get().buffer.seconds);
    buffer_seconds_input->callback(on_buffer_seconds, this);

    selection_seconds_input = seconds_input(left, y, right - left, "Seconds selected at first",
                                            "The end of a capture, to send to whisper as it is.");
    selection_seconds_input->value(settings.get().buffer.selection_seconds);
    selection_seconds_input->callback(on_buffer_seconds, this);

    buffer_status = new Fl_Box(left, y, right - left, 20);
    buffer_status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    y += buffer_status->h() + 14;

    // The command to bind to a key, in a box it can be copied from. None
    // on Windows, where the shortcut will be a setting of its own.
    const std::string command = capture_command();
    if (command.empty()) return;

    Fl_Box* label = new Fl_Box(left, y, right - left, 20, "Capture shortcut");
    label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += label->h() + 4;

    auto* command_box = new ClipboardInput(left, y, right - left, PILL_H);
    command_box->value(command.c_str());
    command_box->readonly(1);
    y += PILL_H + 6;

    // Wayland lets no app grab a key for itself
    Fl_Box* note = new Fl_Box(left, y, right - left, 32,
                              "Bind a key to it in your desktop's settings, e.g. in hyprland.conf:\n"
                              "bind = SUPER, A, exec, <the command>");
    note->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE);
    note->labelsize(12);
    note->labelcolor(DIM);
    y += note->h() + 4;

    hotkey_status = new Fl_Box(left, y, right - left, 20);
    hotkey_status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    hotkey_status->labelcolor(ERROR_COLOR);
}

void SettingsWindow::on_buffer_enabled(Fl_Widget*, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    const bool enabled = self->buffer_enabled->value();
    self->settings.update([enabled](Settings& s){ s.buffer.enabled = enabled; });
}

void SettingsWindow::on_buffer_seconds(Fl_Widget*, void* data){
    static_cast<SettingsWindow*>(data)->apply_buffer_seconds();
}

void SettingsWindow::apply_buffer_seconds(){
    // Empty, or not a number: put back as it was
    const BufferSettings& saved = settings.get().buffer;
    int seconds = saved.seconds, selection = saved.selection_seconds;
    try {
        seconds = std::stoi(buffer_seconds_input->value());
    } catch (const std::exception&){}
    try {
        selection = std::stoi(selection_seconds_input->value());
    } catch (const std::exception&){}

    if (seconds != saved.seconds || selection != saved.selection_seconds)
        settings.update([seconds, selection](Settings& s){
            s.buffer.seconds = seconds;
            s.buffer.selection_seconds = selection;
        });
    // Clamped by update(), and shown that way
    buffer_seconds_input->value(settings.get().buffer.seconds);
    selection_seconds_input->value(settings.get().buffer.selection_seconds);
}

void SettingsWindow::refresh_buffer(){
    buffer_enabled->value(settings.get().buffer.enabled);
    if (capturer.recording()){
        buffer_status->copy_label(std::format("Recording. The last {} seconds are kept.",
                                              settings.get().buffer.seconds).c_str());
        buffer_status->labelcolor(DIM);
    } else if (!settings.get().buffer.enabled){
        buffer_status->copy_label("Off. Nothing is recorded.");
        buffer_status->labelcolor(DIM);
    } else {
        buffer_status->copy_label(std::format("Not recording: {}", capturer.error()).c_str());
        buffer_status->labelcolor(ERROR_COLOR);
    }
    buffer_status->redraw();

    if (hotkey_status){
        hotkey_status->copy_label(capturer.hotkey_error().c_str());
        hotkey_status->redraw();
    }
}

// A label on the left, and a dropdown filling the rest of the row
static Fl_Choice* labelled_choice(int left, int y, int w, const char* label_text){
    constexpr int LABEL_W = 90;
    Fl_Box* label = new Fl_Box(left, y, LABEL_W, PILL_H, label_text);
    label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    auto* choice = new Fl_Choice(left + LABEL_W, y, w - LABEL_W, PILL_H);
    choice->clear_visible_focus();
    return choice;
}

// The deck and note type, then a dropdown per part of a card for the field
// it goes in, then a button that asks Anki again and whether it answered
void SettingsWindow::add_anki_settings(int x, int w){
    const int left = x + MARGIN, right = x + w - MARGIN;
    constexpr int ROW_H = PILL_H + 4;
    // Lines up with the first category in the sidebar, like the API page
    int y = MARGIN + (CATEGORY_H - PILL_H) / 2;

    deck_choice = labelled_choice(left, y, right - left, "Deck");
    deck_choice->callback(on_deck_chosen, this);
    y += ROW_H;
    note_type_choice = labelled_choice(left, y, right - left, "Note type");
    note_type_choice->callback(on_note_type_chosen, this);
    y += ROW_H + 6;

    Fl_Box* note = new Fl_Box(left, y, right - left, 18, "Which field of the note type gets each part of a card:");
    note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    note->labelsize(12);
    note->labelcolor(DIM);
    y += note->h() + 2;

    for (size_t i = 0; i < CARD_PART_COUNT; ++i){
        field_choices[i] = labelled_choice(left, y, right - left, CARD_PART_NAMES[i]);
        field_choices[i]->callback(on_field_chosen, this);
        y += ROW_H;
    }
    y += 4;

    auto* reload = new Fl_Button(left, y, CLEAR_BTN_W, MODEL_BTN_H, "Ask Anki again");
    reload->box(FL_ROUND_UP_BOX);
    reload->clear_visible_focus();
    reload->callback(on_anki_reload, this);

    // Under the button, since an error can take two lines
    y += MODEL_BTN_H + 4;
    anki_status = new Fl_Box(left, y, right - left, win.h() - y - 4);
    anki_status->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    anki_status->labelsize(12);
}

// Lists names in choice with saved selected. saved stays in the list even
// if names doesn't have it, e.g. Anki isn't open, so the choice isn't lost.
// With none_label, the first item is that and saves "". Returns what each
// item saves.
static std::vector<std::string> list_names(Fl_Choice& choice, std::vector<std::string> names,
                                           const std::string& saved, const char* none_label){
    if (!saved.empty() && std::ranges::find(names, saved) == names.end()) names.push_back(saved);
    if (none_label) names.insert(names.begin(), "");

    std::vector<std::string> labels = names;
    if (none_label) labels.front() = none_label;
    const auto selected = std::ranges::find(names, saved);
    set_items(choice, labels, selected == names.end() ? -1 : static_cast<int>(selected - names.begin()));
    return names;
}

void SettingsWindow::refresh_anki(){
    const AnkiSettings& saved = settings.get().anki;
    std::vector<std::string> decks, note_types;
    // Blocks until Anki answers, which is quick: it's on this machine, and
    // when it isn't open the connection is refused at once
    try {
        decks = anki_deck_names();
        note_types = anki_model_names();
        anki_status->copy_label("Connected to Anki.");
        anki_status->labelcolor(DIM);
    } catch (const std::exception& e){
        anki_status->copy_label(e.what());
        anki_status->labelcolor(ERROR_COLOR);
    }
    anki_status->redraw();
    anki_listed = true;

    deck_names = list_names(*deck_choice, decks, saved.deck, nullptr);
    note_type_names = list_names(*note_type_choice, note_types, saved.note_type, nullptr);
    field_names.clear();
    if (!saved.note_type.empty() && std::ranges::find(note_types, saved.note_type) != note_types.end()){
        try {
            field_names = anki_model_field_names(saved.note_type);
        } catch (const std::exception&){
            // Only the saved ones are listed then
        }
    }
    refresh_fields();
}

void SettingsWindow::refresh_fields(){
    const AnkiSettings& saved = settings.get().anki;
    for (size_t i = 0; i < CARD_PART_COUNT; ++i){
        const std::string& field = field_for(saved, static_cast<CardPart>(i));
        list_names(*field_choices[i], field_names, field, "(none)");
    }
}

void SettingsWindow::on_deck_chosen(Fl_Widget*, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    const int i = self->deck_choice->value();
    if (i < 0 || i >= static_cast<int>(self->deck_names.size())) return;
    const std::string deck = self->deck_names[static_cast<size_t>(i)];
    self->settings.update([&deck](Settings& s){ s.anki.deck = deck; });
}

void SettingsWindow::on_note_type_chosen(Fl_Widget*, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    const int i = self->note_type_choice->value();
    if (i < 0 || i >= static_cast<int>(self->note_type_names.size())) return;
    const std::string note_type = self->note_type_names[static_cast<size_t>(i)];
    if (note_type == self->settings.get().anki.note_type) return;

    try {
        self->field_names = anki_model_field_names(note_type);
    } catch (const std::exception& e){
        self->field_names.clear();
        fl_alert("Couldn't list the note type's fields:\n\n%s", e.what());
    }
    // The old note type's fields mean nothing in this one, so each part
    // goes where its name fits best, to be checked and changed
    const auto guesses = guess_fields(self->field_names);
    self->settings.update([&](Settings& s){
        s.anki.note_type = note_type;
        for (size_t p = 0; p < CARD_PART_COUNT; ++p) field_for(s.anki, static_cast<CardPart>(p)) = guesses[p];
    });
    self->refresh_fields();
}

void SettingsWindow::on_field_chosen(Fl_Widget* w, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    const auto part = static_cast<size_t>(std::ranges::find(self->field_choices, w) - self->field_choices.begin());
    auto* choice = static_cast<Fl_Choice*>(w);
    const int i = choice->value();
    // The items are as list_names made them: "(none)", then field_names,
    // then maybe a saved field the note type doesn't have
    std::vector<std::string> names = self->field_names;
    names.insert(names.begin(), "");
    const std::string& saved = field_for(self->settings.get().anki, static_cast<CardPart>(part));
    if (!saved.empty() && std::ranges::find(names, saved) == names.end()) names.push_back(saved);
    if (i < 0 || i >= static_cast<int>(names.size())) return;

    const std::string field = names[static_cast<size_t>(i)];
    self->settings.update([&](Settings& s){ field_for(s.anki, static_cast<CardPart>(part)) = field; });
}

void SettingsWindow::on_anki_reload(Fl_Widget*, void* data){
    static_cast<SettingsWindow*>(data)->refresh_anki();
}
