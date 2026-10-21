#pragma once

// FLTK
#include <FL/Fl_Double_Window.H>

class AppState {
public:
    AppState();

    // One app, one window: copying makes no sense
    AppState(const AppState&) = delete;
    AppState& operator=(const AppState&) = delete;

    void show(int argc, char** argv);

private:
    // Owned by AppState as a member, so no new/delete anywhere.
    // When AppState dies, main_win dies and deletes all of its children.
    Fl_Double_Window main_win{900, 600, "Prototype"};
};
