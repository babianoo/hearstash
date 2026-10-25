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
#include <FL/Fl_Group.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Round_Button.H>
#include <FL/Fl_SVG_Image.H>

#include "core/whisper_models.h"
#include "ui/clipboard_input.h"
#include "ui/icons.h"
#include "ui/model_downloader.h"
#include "ui/settings_store.h"
#include "ui/theme.h"

// Same categories as japanese-helper, in sidebar order
static constexpr const char* CATEGORY_NAMES[] = {"General", "History", "API", "Speech-to-Text"};
// Indexes into CATEGORY_NAMES
enum Category : size_t { GENERAL, HISTORY, API, SPEECH_TO_TEXT };

static constexpr int SIDEBAR_W = 160;
static constexpr int CATEGORY_H = 36;
// Space around the sidebar's list and a page's contents
static constexpr int MARGIN = 20;

static const Fl_Color DIM = fl_rgb_color(70, 70, 70);
static const Fl_Color ACCENT = fl_rgb_color(50, 110, 240);
static const Fl_Color ERROR_COLOR = fl_rgb_color(200, 40, 40);

// A row on the Speech-to-Text page, and its button
static constexpr int MODEL_ROW_H = 34;
static constexpr int MODEL_BTN_W = 80, MODEL_BTN_H = 26;
// The size or progress column, left of the button
static constexpr int MODEL_STATUS_W = 110;

// Where model is in WHISPER_MODELS, and so in model_rows
static size_t index_of(const WhisperModel& model){
    return static_cast<size_t>(&model - WHISPER_MODELS.data());
}

// An entry in the sidebar: plain text, no button look. The selected one is
// bold, with an accent bar along its right edge.
//
// A radio button, so selecting one deselects the others in its group. Its
// callback only runs when the selection changes: clicking the one that's
// already selected does nothing.
class CategoryButton : public Fl_Button {
public:
    CategoryButton(int x, int y, int w, int h, const char* label) : Fl_Button(x, y, w, h, label){
        type(FL_RADIO_BUTTON);
        clear_visible_focus();    // No dotted focus ring after a click
    }

    void draw() override {
        // The sidebar's color, so it looks like part of it
        draw_box(FL_FLAT_BOX, parent()->color());
        fl_font(value() ? FL_HELVETICA_BOLD : FL_HELVETICA, labelsize());
        fl_color(value() ? FL_FOREGROUND_COLOR : DIM);
        fl_draw(label(), x() + MARGIN, y(), w() - MARGIN, h(), FL_ALIGN_LEFT);
        if (value()){
            fl_color(ACCENT);
            fl_rectf(x() + w() - 3, y(), 3, h());
        }
    }
};

SettingsWindow::SettingsWindow(SettingsStore& settings, ModelDownloader& downloader)
    : settings(settings), downloader(downloader){
    // win is the current group here, like in MainWindow

    // A bit darker than the pages, so it stands apart from them. Computed
    // here, not as a constant: the background color can still change
    // (e.g. with -bg) before the first window is created.
    const Fl_Color sidebar_color = fl_color_average(FL_BACKGROUND_COLOR, FL_BLACK, 0.92f);

    // Its own group so the radio buttons only deselect each other
    Fl_Group* sidebar = new Fl_Group(0, 0, SIDEBAR_W, win.h());
    sidebar->box(FL_FLAT_BOX);
    sidebar->color(sidebar_color);
    for (size_t i = 0; i < std::size(CATEGORY_NAMES); ++i){
        const int y = MARGIN + static_cast<int>(i) * CATEGORY_H;
        Fl_Button* category = new CategoryButton(0, y, SIDEBAR_W, CATEGORY_H, CATEGORY_NAMES[i]);
        category->callback(on_category, this);
        categories.push_back(category);
    }
    sidebar->end();

    const int page_x = SIDEBAR_W, page_w = win.w() - SIDEBAR_W;
    for (size_t i = 0; i < std::size(CATEGORY_NAMES); ++i){
        Fl_Group* page = new Fl_Group(page_x, 0, page_w, win.h());
        if (i == API) add_api_settings(page_x, page_w);
        if (i == SPEECH_TO_TEXT) add_speech_settings(page_x, page_w);

        page->end();
        page->hide();    // Shown by select()
        pages.push_back(page);
    }

    select(0);

    // No resizable() on purpose: FLTK then keeps the window at a fixed size.
    win.end();

    // While the window is closed too, so a download that finishes then
    // still becomes the chosen model if there's none
    downloader_listener = downloader.add_listener([this](const WhisperModel& model){ refresh_model(model); });
}

SettingsWindow::~SettingsWindow(){
    downloader.remove_listener(downloader_listener);
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
}

void SettingsWindow::show(){
    // A model may have been deleted or added outside the app meanwhile
    refresh_models();
    win.show();
}

void SettingsWindow::on_category(Fl_Widget* w, void* data){
    SettingsWindow* self = static_cast<SettingsWindow*>(data);
    const auto it = std::ranges::find(self->categories, w);
    self->select(it - self->categories.begin());
}

void SettingsWindow::select(size_t i){
    // Already done when the category was clicked, but not when it's
    // selected from code
    categories[i]->setonly();
    for (size_t p = 0; p < pages.size(); ++p){
        if (p == i) pages[p]->show();
        else pages[p]->hide();
    }
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
