#include "ui/main_window.h"

// FLTK
#include <FL/Fl_Box.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>

#include "ui/theme.h"

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

    // Starts after the pill's curved left end (radius h / 2) and stops a
    // little before the button. Created before the button so it's the
    // widget that gets keyboard focus when the window opens.
    const int btn_x = bar_x + bar_w - btn_w - pad;
    const int input_x = bar_x + h / 2;
    search_input = new Fl_Input(input_x, bar_y + 2, btn_x - 6 - input_x, h - 4);
    search_input->box(FL_FLAT_BOX);

    search_btn = new Fl_Button(btn_x, bar_y + pad, btn_w, h - 2 * pad, "Search");
    search_btn->box(FL_ROUND_UP_BOX);

    // No resizable() on purpose: FLTK then keeps the window at a fixed size.
    win.end();
}

void MainWindow::show(int argc, char** argv){
    win.show(argc, argv);
}
