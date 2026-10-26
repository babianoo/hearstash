#pragma once

// FLTK
#include <FL/Fl_Double_Window.H>

// The window the clock button opens, with the search history. Empty for
// now.
class HistoryWindow {
public:
    HistoryWindow();

    // One history window per app: copying makes no sense
    HistoryWindow(const HistoryWindow&) = delete;
    HistoryWindow& operator=(const HistoryWindow&) = delete;

    // Opens the window, or raises it if it's already open
    void show();

private:
    // Owned by HistoryWindow as a member, so no new/delete anywhere.
    // Closing the window only hides it.
    Fl_Double_Window win{760, 540, "History"};
};
