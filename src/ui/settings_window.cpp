#include "ui/settings_window.h"

SettingsWindow::SettingsWindow(){
    // win is the current group here, like in MainWindow. Nothing is added
    // to it yet.

    // No resizable() on purpose: FLTK then keeps the window at a fixed size.
    win.end();
}

void SettingsWindow::show(){
    win.show();
}
