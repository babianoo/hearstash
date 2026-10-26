#include "ui/history_window.h"

HistoryWindow::HistoryWindow(){
    // win is the current group here, like in MainWindow

    // No resizable() on purpose: FLTK then keeps the window at a fixed size.
    win.end();
}

void HistoryWindow::show(){
    win.show();
}
