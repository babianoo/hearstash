#include "ui/main_window.h"

#include <iterator>

// FLTK
#include <FL/Fl_Box.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Menu_Item.H>

#include "ui/theme.h"

// Indexed by Api
static constexpr const char* API_NAMES[] = {"Jisho", "DeepL", "MyMemory"};

MainWindow::MainWindow(){
    // win is constructed (as a member) before this body runs, so it's
    // the current group here: widgets created below are added to it.

    // Google-style search bar, centered a bit above the middle, with the
    // Search button inside its right end.
    const int bar_w = 600, btn_w = 80, h = PILL_H, pad = 3;
    const int bar_x = (win.w() - bar_w) / 2;
    const int bar_y = win.h() * 2 / 5;

    // FLTK doesn't handle overlapping widgets: when the input redraws (e.g.
    // on focus) it would paint over a button placed on top of it. So the
    // pill is just a background box, and the input and button sit side by
    // side inside it. The input ending before the button also means typed
    // text scrolls instead of disappearing under the button.
    Fl_Box* bar = new Fl_Box(bar_x, bar_y, bar_w, h);
    bar->box(FL_ROUND_UP_BOX);
    bar->color(FL_BACKGROUND2_COLOR);    // Same white as the input's fill

    // API button in the pill's left end: a circle as tall as the Search
    // button, and concentric with the pill's curve.
    const int circle_d = h - 2 * pad;
    const int circle_x = bar_x + pad;

    // Faint divider between the API button and the input
    const int divider_x = circle_x + circle_d + 4;

    // Starts a little after the circle and stops a little before the
    // button. Created before the button so it's the widget that gets
    // keyboard focus when the window opens.
    const int btn_x = bar_x + bar_w - btn_w - pad;
    const int input_x = divider_x + 1 + 6;
    search_input = new Fl_Input(input_x, bar_y + 2, btn_x - 6 - input_x, h - 4);
    search_input->box(FL_FLAT_BOX);

    search_btn = new Fl_Button(btn_x, bar_y + pad, btn_w, h - 2 * pad, "Search");
    search_btn->box(FL_ROUND_UP_BOX);

    // 1px line, 85% of the way from the text color to the input's white,
    // so it's barely visible.
    Fl_Box* divider = new Fl_Box(divider_x, bar_y + 8, 1, h - 16);
    divider->box(FL_FLAT_BOX);
    divider->color(fl_color_average(FL_BACKGROUND2_COLOR, FL_FOREGROUND_COLOR, 0.85f));

    // "+" that opens the API menu. No box, so the pill shows through; it
    // only draws a round grey circle while pressed. (A white FL_FLAT_BOX
    // would be square and cover the pill's curved edge.) The tooltip shows
    // the current API.
    api_btn = new Fl_Button(circle_x, bar_y + pad, circle_d, circle_d, "+");
    api_btn->box(FL_NO_BOX);
    api_btn->down_box(FL_ROUND_DOWN_BOX);
    api_btn->tooltip(API_NAMES[static_cast<int>(api)]);
    api_btn->clear_visible_focus();      // No dotted focus ring after a click
    api_btn->callback(on_api_btn, this);

    // No resizable() on purpose: FLTK then keeps the window at a fixed size.
    win.end();
}

void MainWindow::on_api_btn(Fl_Widget* w, void* data){
    MainWindow* self = static_cast<MainWindow*>(data);
    const int current = static_cast<int>(self->api);

    // Rebuilt on every click so the radio check follows the current API.
    // Zeroed first: the extra all-zero item marks the end of the menu.
    Fl_Menu_Item items[std::size(API_NAMES) + 1]{};
    for (int i = 0; i < static_cast<int>(std::size(API_NAMES)); ++i){
        items[i].label(API_NAMES[i]);
        items[i].flags = FL_MENU_RADIO | (i == current ? FL_MENU_VALUE : 0);
    }

    // Opens just below the button. nullptr if the user clicks away.
    const Fl_Menu_Item* picked = items->popup(w->x(), w->y() + w->h(), nullptr, &items[current]);
    if (picked == nullptr) return;

    const int index = static_cast<int>(picked - items);
    self->api = static_cast<Api>(index);
    w->tooltip(API_NAMES[index]);
}

void MainWindow::show(int argc, char** argv){
    win.show(argc, argv);
}
