#pragma once

// FLTK
#include <FL/Fl_Double_Window.H>

// Only a pointer to it is stored here, so the full header is included in
// main_window.cpp instead.
class SearchBar;

class MainWindow {
public:
    MainWindow();

    // One app, one window: copying makes no sense
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void show(int argc, char** argv);

private:
    // Owned by MainWindow as a member, so no new/delete anywhere.
    // When MainWindow dies, win dies and deletes all of its children.
    Fl_Double_Window win{900, 600, "Prototype"};

    // Created with new in the constructor and added to win, which
    // deletes it when it dies. MainWindow only keeps a handle to it.
    SearchBar* search_bar = nullptr;
};
