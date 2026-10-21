#include "ui/main_window.h"

// FLTK
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>

#include "ui/theme.h"

MainWindow::MainWindow(){
    // win is constructed (as a member) before this body runs, so it's
    // the current group here: widgets created below are added to it.

    // Google-style layout: input centered a bit above the middle,
    // button centered under it.
    const int input_w = 600, btn_w = 80, h = PILL_H;
    const int input_y = win.h() * 2 / 5;

    search_input = new Fl_Input((win.w() - input_w) / 2, input_y, input_w, h);
    search_input->box(PILL_BOX);

    search_btn = new Fl_Button((win.w() - btn_w) / 2, input_y + h + 20, btn_w, h, "Search");
    search_btn->box(FL_ROUNDED_BOX);

    win.end();
    win.resizable(win);
}

void MainWindow::show(int argc, char** argv){
    win.show(argc, argv);
}
