#include "ui/main_window.h"

#include "ui/search_bar.h"

MainWindow::MainWindow(){
    // win is constructed (as a member) before this body runs, so it's
    // the current group here: widgets created below are added to it.

    // Centered, a bit above the middle
    const int bar_w = 600;
    search_bar = new SearchBar((win.w() - bar_w) / 2, win.h() * 2 / 5, bar_w);

    // No resizable() on purpose: FLTK then keeps the window at a fixed size.
    win.end();
}

void MainWindow::show(int argc, char** argv){
    win.show(argc, argv);
}
