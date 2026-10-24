#include "ui/settings_window.h"

#include <algorithm>
#include <iterator>

// FLTK
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <FL/fl_draw.H>

// Same categories as japanese-helper, in sidebar order
static constexpr const char* CATEGORY_NAMES[] = {"General", "History", "API", "Speech-to-Text"};

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

SettingsWindow::SettingsWindow(){
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
    for (const char* name : CATEGORY_NAMES){
        Fl_Group* page = new Fl_Group(page_x, 0, page_w, win.h());

        // Placeholder until the page has settings: shows which page it is
        Fl_Box* heading = new Fl_Box(page_x + MARGIN, MARGIN, page_w - 2 * MARGIN, CATEGORY_H, name);
        heading->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        heading->labelfont(FL_HELVETICA_BOLD);
        heading->labelsize(18);

        page->end();
        page->hide();    // Shown by select()
        pages.push_back(page);
    }

    select(0);

    // No resizable() on purpose: FLTK then keeps the window at a fixed size.
    win.end();
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
