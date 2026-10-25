#include "ui/settings_window.h"

#include <algorithm>
#include <iterator>

// FLTK
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <FL/fl_draw.H>
#include <FL/Fl_SVG_Image.H>

#include "ui/clipboard_input.h"
#include "ui/icons.h"
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

        page->end();
        page->hide();    // Shown by select()
        pages.push_back(page);
    }

    select(0);

    // No resizable() on purpose: FLTK then keeps the window at a fixed size.
    win.end();
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

void SettingsWindow::show(){
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
