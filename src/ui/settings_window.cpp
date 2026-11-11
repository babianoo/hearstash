#include "ui/settings_window.h"

#include <algorithm>
#include <exception>
#include <format>
#include <initializer_list>
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
#include "core/hearstash_note_type.h"
#include "core/recorder.h"
#include "core/settings.h"
#include "core/transcriber.h"
#include "core/whisper_models.h"
#include "ui/capturer.h"
#include "ui/category_button.h"
#include "ui/clipboard_input.h"
#include "ui/global_hotkey.h"
#include "ui/history_store.h"
#include "ui/icons.h"
#include "ui/model_downloader.h"
#include "ui/settings_store.h"
#include "ui/shortcut_input.h"
#include "ui/theme.h"

// In sidebar order. General has the DeepL key, the history and scrolling;
// Recording the rolling buffer and stashing; Shortcuts all three hotkeys.
static constexpr const char* CATEGORY_NAMES[] = {"General", "Speech-to-Text", "Recording", "Shortcuts", "Anki"};
// Indexes into CATEGORY_NAMES
enum Category : size_t { GENERAL, SPEECH_TO_TEXT, RECORDING, SHORTCUTS, ANKI };

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

// On the General page: the box for how many searches to keep, wide
// enough for 1000, and the button that clears them
static constexpr int CAPACITY_W = 80;
static constexpr int CLEAR_BTN_W = 120;

// On the Shortcuts page: a shortcut's box, wide enough for
// Win+Ctrl+Alt+Shift+PageDown
static constexpr int SHORTCUT_W = 220;

// Most settings are a row: a label on the left, LABEL_W wide, and its box
// right of it. Rows are ROW_H apart, groups of them GROUP_GAP more.
static constexpr int LABEL_W = 120;
static constexpr int ROW_H = PILL_H + 6;
static constexpr int GROUP_GAP = 14;

// A row's label. What a setting does in more detail goes in a tooltip, on
// the label and its box, rather than a line of its own under them.
static Fl_Box* row_label(int left, int y, const char* text){
    Fl_Box* label = new Fl_Box(left, y, LABEL_W, PILL_H, text);
    label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    return label;
}

static void set_tip(std::initializer_list<Fl_Widget*> widgets, const std::string& tip){
    for (Fl_Widget* w : widgets) w->copy_tooltip(tip.c_str());
}

// A small grey line, e.g. a status under a setting
static Fl_Box* dim_line(int x, int y, int w, int h, const char* text = nullptr){
    Fl_Box* line = new Fl_Box(x, y, w, h, text);
    line->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    line->labelsize(12);
    line->labelcolor(DIM);
    return line;
}

// A box for a whole number, applied on Enter or leaving it: on every
// keystroke, typing 150 would go through 1, which would e.g. delete all but
// the newest searches
static ClipboardInput* number_input(int x, int y){
    auto* input = new ClipboardInput(x, y, CAPACITY_W, PILL_H);
    input->type(FL_INT_INPUT);
    input->when(FL_WHEN_ENTER_KEY | FL_WHEN_RELEASE);
    return input;
}

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
        if (i == GENERAL) add_general_settings(page_x, page_w);
        if (i == SPEECH_TO_TEXT) add_speech_settings(page_x, page_w);
        if (i == RECORDING) add_recording_settings(page_x, page_w);
        if (i == SHORTCUTS) add_shortcut_settings(page_x, page_w);
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
    apply_scroll_speed();
    apply_capacity();
    apply_buffer_seconds();
    apply_quality();
    history.remove_listener(history_listener);
    downloader.remove_listener(downloader_listener);
    settings.remove_listener(settings_listener);
}

// The DeepL key with an eye button on its right, and whether captures are
// translated by themselves. Then how many searches to keep, with the
// button that clears them and how many there are. Then the mouse wheel's
// scroll speed.
void SettingsWindow::add_general_settings(int x, int w){
    const int left = x + MARGIN, right = x + w - MARGIN;
    const int box_x = left + LABEL_W;
    // Lines up with the first category in the sidebar
    int y = MARGIN + (CATEGORY_H - PILL_H) / 2;

    Fl_Box* key_label = row_label(left, y, "DeepL API key");
    // Same height as the search bar; the button is square, like the gear
    const int eye_x = right - PILL_H;
    key_input = new ClipboardInput(box_x, y, eye_x - 6 - box_x, PILL_H);
    key_input->value(settings.get().api.deepl_key.c_str());
    // On every keystroke, so the key is in use as soon as it's typed
    key_input->when(FL_WHEN_CHANGED);
    key_input->callback(on_key_changed, this);
    set_tip({key_label, key_input}, "If empty, the DEEPL_API_KEY environment variable is used.");

    key_visibility_btn = new Fl_Button(eye_x, y, PILL_H, PILL_H);
    key_visibility_btn->box(FL_ROUND_UP_BOX);    // Round, like the gear
    key_visibility_btn->clear_visible_focus();   // No dotted focus ring after a click
    key_visibility_btn->callback(on_key_visibility, this);
    set_key_hidden(true);
    y += ROW_H;

    auto_translate_check = new Fl_Check_Button(box_x, y, right - box_x, PILL_H, "Translate automatically");
    auto_translate_check->value(settings.get().api.auto_translate);
    auto_translate_check->clear_visible_focus();
    auto_translate_check->callback(on_auto_translate, this);
    set_tip({auto_translate_check}, "Sends each transcript and screen text to DeepL as soon as it's there, which "
                                    "counts towards the key's quota.\nOff, only Translate (Ctrl+T) does.");
    y += ROW_H + GROUP_GAP;

    Fl_Box* capacity_label = row_label(left, y, "History size");
    capacity_input = number_input(box_x, y);
    capacity_input->value(settings.get().history.capacity);
    // Also when the page or window is left: see apply_capacity()
    capacity_input->callback(on_capacity, this);
    set_tip({capacity_label, capacity_input},
            std::format("How many searches to keep, from {} to {}. Lowering it deletes the oldest ones over it.",
                        HistorySettings::MIN_CAPACITY, HistorySettings::MAX_CAPACITY));

    const int clear_x = box_x + CAPACITY_W + 8;
    clear_history_btn = new Fl_Button(clear_x, y + (PILL_H - MODEL_BTN_H) / 2, CLEAR_BTN_W, MODEL_BTN_H,
                                      "Clear History");
    clear_history_btn->box(FL_ROUND_UP_BOX);
    clear_history_btn->clear_visible_focus();
    clear_history_btn->callback(on_clear_history, this);

    const int count_x = clear_x + CLEAR_BTN_W + 8;
    history_count = dim_line(count_x, y, right - count_x, PILL_H);
    y += ROW_H + GROUP_GAP;

    Fl_Box* speed_label = row_label(left, y, "Scroll speed");
    scroll_speed_input = number_input(box_x, y);
    scroll_speed_input->value(settings.get().general.scroll_speed);
    scroll_speed_input->callback(on_scroll_speed, this);
    set_tip({speed_label, scroll_speed_input},
            std::format("Pixels per mouse wheel notch, from {} to {}.",
                        GeneralSettings::MIN_SCROLL_SPEED, GeneralSettings::MAX_SCROLL_SPEED));
}

void SettingsWindow::on_scroll_speed(Fl_Widget*, void* data){
    static_cast<SettingsWindow*>(data)->apply_scroll_speed();
}

void SettingsWindow::apply_scroll_speed(){
    // Empty, or not a number: put back as it was
    int speed = settings.get().general.scroll_speed;
    try {
        speed = std::stoi(scroll_speed_input->value());
    } catch (const std::exception&){}

    if (speed != settings.get().general.scroll_speed)
        settings.update([speed](Settings& s){ s.general.scroll_speed = speed; });
    // Clamped by update(), and shown that way
    scroll_speed_input->value(settings.get().general.scroll_speed);
}

void SettingsWindow::refresh_history(){
    const size_t count = history.entries().size();
    history_count->copy_label(std::format("{} saved", count).c_str());
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
    self->apply_scroll_speed();
    self->apply_capacity();
    self->apply_buffer_seconds();
    self->apply_quality();
    for (ShortcutInput* input : self->shortcut_inputs)
        if (input) input->stop_waiting();
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

// Whisper models: a heading, then a row per model: a radio button
// labelled with the model's name, its size, and a button. Under them
// whether it runs on the GPU, and the microphone.
void SettingsWindow::add_speech_settings(int x, int w){
    const int left = x + MARGIN, right = x + w - MARGIN;
    // Lines up with the first category in the sidebar, like the General page
    int y = MARGIN + (CATEGORY_H - 20) / 2;

    Fl_Box* label = new Fl_Box(left, y, right - left, 20, "Whisper model");
    label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    std::string models_tip = "Larger models are more accurate, but slower and use more memory.";
    // Not worth failing over: the folder is only shown, not needed here
    try {
        models_tip += std::format("\nSaved in {}", models_dir().string());
    } catch (const std::exception&){}
    set_tip({label}, models_tip);
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
    y += GROUP_GAP;

    // Whether whisper runs on the GPU, and which one. Without one, e.g. in
    // a build without GPU support, it can't be turned on.
    const std::string gpu = Transcriber::gpu_name();
    gpu_check = new Fl_Check_Button(left, y, right - left, PILL_H);
    // In the label when there's none: an inactive button shows no tooltip
    const std::string gpu_label = gpu.empty() ? std::string("Transcribe on the GPU (none found)")
                                              : std::format("Transcribe on the GPU ({})", gpu);
    gpu_check->copy_label(gpu_label.c_str());
    gpu_check->value(!gpu.empty() && settings.get().speech.use_gpu);
    gpu_check->clear_visible_focus();
    gpu_check->callback(on_gpu_toggled, this);
    set_tip({gpu_check}, "Much faster, but takes video memory while the model is loaded.");
    if (gpu.empty()) gpu_check->deactivate();
    y += ROW_H;

    // Filled by refresh_microphones(), since what's plugged in changes
    row_label(left, y, "Microphone");
    mic_choice = new Fl_Choice(left + LABEL_W, y, right - left - LABEL_W, PILL_H);
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
    // Leaving the General, Recording or Anki page is done with it too. The box keeps focus while hidden, so it wouldn't hear about it
    // otherwise.
    apply_scroll_speed();
    apply_capacity();
    apply_buffer_seconds();
    apply_quality();
    for (ShortcutInput* input : shortcut_inputs)
        if (input) input->stop_waiting();
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

void SettingsWindow::on_auto_translate(Fl_Widget*, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    const bool on = self->auto_translate_check->value();
    // From the next transcript or screen read on
    self->settings.update([on](Settings& s){ s.api.auto_translate = on; });
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

// Whether the rolling buffer records, how long it keeps and how much a
// capture starts with selected, and whether it's recording now. Then what
// happens when a line is stashed.
void SettingsWindow::add_recording_settings(int x, int w){
    const int left = x + MARGIN, right = x + w - MARGIN;
    const int box_x = left + LABEL_W;
    // Lines up with the first category in the sidebar, like the General page
    int y = MARGIN + (CATEGORY_H - PILL_H) / 2;

    buffer_enabled = new Fl_Check_Button(left, y, right - left, PILL_H,
                                         "Keep the last seconds of what the computer plays");
    buffer_enabled->value(settings.get().buffer.enabled);
    buffer_enabled->clear_visible_focus();
    buffer_enabled->callback(on_buffer_enabled, this);
    set_tip({buffer_enabled}, "Only kept in memory. Nothing is saved unless you stash it or add it to a card.");
    y += ROW_H;

    // Like the capacity: typing 60 goes through 6, which would forget what
    // was recorded twice
    Fl_Box* seconds_label = row_label(left, y, "Seconds to keep");
    buffer_seconds_input = number_input(box_x, y);
    buffer_seconds_input->value(settings.get().buffer.seconds);
    buffer_seconds_input->callback(on_buffer_seconds, this);
    set_tip({seconds_label, buffer_seconds_input},
            std::format("From {} to {}. Changing it forgets what was kept.",
                        BufferSettings::MIN_SECONDS, BufferSettings::MAX_SECONDS));
    y += ROW_H;

    Fl_Box* selection_label = row_label(left, y, "Selected at first");
    selection_seconds_input = number_input(box_x, y);
    selection_seconds_input->value(settings.get().buffer.selection_seconds);
    selection_seconds_input->callback(on_buffer_seconds, this);
    set_tip({selection_label, selection_seconds_input},
            "Seconds at the end of a capture that start selected, to send to whisper as they are.");
    y += ROW_H;

    buffer_status = new Fl_Box(left, y, right - left, 20);
    buffer_status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    buffer_status->labelsize(12);
    y += buffer_status->h() + GROUP_GAP;

    const std::string stash_tip = "A stashed line is saved on this computer, its audio and screenshot, until a "
                                  "card is made from it or it's deleted.\nAbout 3 MB for 30 seconds.";
    stash_chime_check = new Fl_Check_Button(left, y, right - left, PILL_H, "Chime when a line is stashed");
    stash_chime_check->value(settings.get().buffer.stash_chime);
    stash_chime_check->clear_visible_focus();
    stash_chime_check->callback(on_stash_chime, this);
    set_tip({stash_chime_check}, stash_tip);
    y += ROW_H;

    stash_transcribe_check = new Fl_Check_Button(left, y, right - left, PILL_H,
                                                 "Transcribe stashed lines right away");
    stash_transcribe_check->value(settings.get().buffer.stash_transcribe);
    stash_transcribe_check->clear_visible_focus();
    stash_transcribe_check->callback(on_stash_transcribe, this);
    set_tip({stash_transcribe_check}, "So the stash shows what each line says. Whisper then runs while you "
                                      "watch, which can slow down a game.");
}

// The label, and what the shortcut does, for each HotkeyAction
struct HotkeyText {
    const char* label;
    const char* tip;
};
static constexpr HotkeyText HOTKEY_TEXTS[HOTKEY_ACTION_COUNT] = {
    {"Capture", "Opens what the computer just played, to transcribe, look up and add to Anki."},
    {"Read screen", "Reads the text on the screen, e.g. in a game or visual novel. Drag around the text box "
                    "to read only that; it's kept for the game.\nThe models that read it (PaddleOCR, about "
                    "30 MB) are downloaded the first time."},
    {"Stash", "Saves what a capture would open without stopping anything, to look at after the episode.\n"
              "Open them with the stash button in the main window."},
};

// The setting that holds action's shortcut on Windows, in s, which is
// Settings or const Settings
template <typename S>
static auto& shortcut_setting(S& s, HotkeyAction action){
    switch (action){
    case HotkeyAction::Capture: return s.buffer.shortcut;
    case HotkeyAction::ReadScreen: return s.screen_text.shortcut;
    case HotkeyAction::Stash: return s.buffer.stash_shortcut;
    }
    return s.buffer.shortcut;
}

// A row per shortcut, each with a line under it for why it can't be
// received, then a note on how to set them
void SettingsWindow::add_shortcut_settings(int x, int w){
    const int left = x + MARGIN, right = x + w - MARGIN;
    const int box_x = left + LABEL_W;
    // Lines up with the first category in the sidebar, like the General page
    int y = MARGIN + (CATEGORY_H - PILL_H) / 2;

    // On Linux, the command to bind to a key, in a box it can be copied
    // from. None on Windows, where the app takes the key itself.
    const bool commands = !hotkey_command(HotkeyAction::Capture).empty();
    for (HotkeyAction action : HOTKEY_ACTIONS){
        const size_t index = static_cast<size_t>(action);
        const HotkeyText& text = HOTKEY_TEXTS[index];
        Fl_Box* label = row_label(left, y, text.label);

        Fl_Widget* box;
        if (commands){
            auto* command_box = new ClipboardInput(box_x, y, right - box_x, PILL_H);
            command_box->value(hotkey_command(action).c_str());
            command_box->readonly(1);
            box = command_box;
        } else {
            ShortcutInput* input = new ShortcutInput(box_x, y, SHORTCUT_W, PILL_H);
            input->shortcut(shortcut_setting(settings.get(), action));
            input->callback(on_shortcut, this);
            // Otherwise pressing the shortcut there would capture
            input->on_waiting = [this](bool waiting){ capturer.pause_hotkey(waiting); };
            shortcut_inputs[index] = input;
            box = input;
        }
        set_tip({label, box}, text.tip);
        y += PILL_H + 2;

        Fl_Box* status = dim_line(box_x, y, right - box_x, 16);
        status->labelcolor(ERROR_COLOR);
        hotkey_statuses[index] = status;
        y += status->h() + 8;
    }

    // Wayland lets no app grab a key for itself
    Fl_Box* note = dim_line(left, y, right - left, 32,
                            commands ? "Bind a key to each command in your desktop's settings,\n"
                                       "e.g. in hyprland.conf: bind = SUPER, A, exec, <the command>"
                                     : "Click a box, then press the keys. They work in any app, even fullscreen.");
    note->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
}

void SettingsWindow::on_stash_transcribe(Fl_Widget*, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    const bool transcribe = self->stash_transcribe_check->value();
    // From the next stash on; lines already stashed stay as they are
    self->settings.update([transcribe](Settings& s){ s.buffer.stash_transcribe = transcribe; });
}

void SettingsWindow::on_buffer_enabled(Fl_Widget*, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    const bool enabled = self->buffer_enabled->value();
    self->settings.update([enabled](Settings& s){ s.buffer.enabled = enabled; });
}

void SettingsWindow::on_gpu_toggled(Fl_Widget*, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    const bool use_gpu = self->gpu_check->value();
    // The model is loaded again for the next transcription
    self->settings.update([use_gpu](Settings& s){ s.speech.use_gpu = use_gpu; });
}

void SettingsWindow::on_shortcut(Fl_Widget* w, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    for (HotkeyAction action : HOTKEY_ACTIONS){
        ShortcutInput* input = self->shortcut_inputs[static_cast<size_t>(action)];
        if (input != w) continue;
        const std::string shortcut = input->shortcut();
        // The Capturer hears of it and registers it; refresh_buffer() then
        // shows whether that worked
        self->settings.update([&shortcut, action](Settings& s){ shortcut_setting(s, action) = shortcut; });
    }
}

void SettingsWindow::on_stash_chime(Fl_Widget*, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    const bool chime = self->stash_chime_check->value();
    self->settings.update([chime](Settings& s){ s.buffer.stash_chime = chime; });
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

    for (HotkeyAction action : HOTKEY_ACTIONS){
        Fl_Box* status = hotkey_statuses[static_cast<size_t>(action)];
        if (!status) continue;
        const std::string error = capturer.hotkey_error(action);
        status->copy_label(error.c_str());
        // In full, in case it's cut off
        status->copy_tooltip(error.empty() ? nullptr : error.c_str());
        status->redraw();
    }
    if (stash_chime_check) stash_chime_check->value(settings.get().buffer.stash_chime);
    if (stash_transcribe_check) stash_transcribe_check->value(settings.get().buffer.stash_transcribe);
}

// A row's label, and a dropdown filling the rest of the row
static Fl_Choice* labelled_choice(int left, int y, int w, const char* label_text){
    row_label(left, y, label_text);
    auto* choice = new Fl_Choice(left + LABEL_W, y, w - LABEL_W, PILL_H);
    choice->clear_visible_focus();
    return choice;
}

// The deck and note type, then a dropdown per part of a card for the field
// it goes in, then a button that asks Anki again and whether it answered
void SettingsWindow::add_anki_settings(int x, int w){
    const int left = x + MARGIN, right = x + w - MARGIN;
    // Lines up with the first category in the sidebar, like the General page
    int y = MARGIN + (CATEGORY_H - PILL_H) / 2;

    deck_choice = labelled_choice(left, y, right - left, "Deck");
    deck_choice->callback(on_deck_chosen, this);
    y += ROW_H;
    note_type_choice = labelled_choice(left, y, right - left, "Note type");
    note_type_choice->callback(on_note_type_chosen, this);
    set_tip({note_type_choice}, "Each part of a card goes in the note type's field chosen below.");
    y += ROW_H + GROUP_GAP;

    for (size_t i = 0; i < CARD_PART_COUNT; ++i){
        field_choices[i] = labelled_choice(left, y, right - left, CARD_PART_NAMES[i]);
        field_choices[i]->callback(on_field_chosen, this);
        y += ROW_H;
    }

    // Under the Picture field, which it's about
    Fl_Box* quality_label = row_label(left, y, "Picture quality");
    quality_input = number_input(left + LABEL_W, y);
    quality_input->value(settings.get().anki.picture_quality);
    quality_input->callback(on_quality, this);
    set_tip({quality_label, quality_input},
            std::format("The picture's JPEG quality, from {} to {}. Higher is sharper, but takes more space in Anki.",
                        AnkiSettings::MIN_QUALITY, AnkiSettings::MAX_QUALITY));
    y += ROW_H;

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

// The fields of note_type, as Anki has them. The Hearstash note type has
// its own until Anki has it, which is from its first card on.
static std::vector<std::string> note_type_fields(const std::string& note_type){
    try {
        return anki_model_field_names(note_type);
    } catch (const std::exception&){
        if (note_type == HEARSTASH_NOTE_TYPE) return hearstash_fields();
        throw;
    }
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

    // Always there to choose, even before Anki has it
    const bool anki_has_saved = std::ranges::find(note_types, saved.note_type) != note_types.end();
    if (std::ranges::find(note_types, HEARSTASH_NOTE_TYPE) == note_types.end())
        note_types.insert(note_types.begin(), HEARSTASH_NOTE_TYPE);

    deck_names = list_names(*deck_choice, decks, saved.deck, nullptr);
    note_type_names = list_names(*note_type_choice, note_types, saved.note_type, nullptr);
    field_names.clear();
    if (anki_has_saved || saved.note_type == HEARSTASH_NOTE_TYPE){
        try {
            field_names = note_type_fields(saved.note_type);
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
        self->field_names = note_type_fields(note_type);
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

void SettingsWindow::on_quality(Fl_Widget*, void* data){
    static_cast<SettingsWindow*>(data)->apply_quality();
}

void SettingsWindow::apply_quality(){
    // Empty, or not a number: put back as it was
    int quality = settings.get().anki.picture_quality;
    try {
        quality = std::stoi(quality_input->value());
    } catch (const std::exception&){}

    if (quality != settings.get().anki.picture_quality)
        settings.update([quality](Settings& s){ s.anki.picture_quality = quality; });
    // Clamped by update(), and shown that way
    quality_input->value(settings.get().anki.picture_quality);
}
