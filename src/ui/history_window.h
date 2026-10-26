#pragma once

// FLTK
#include <FL/Fl_Double_Window.H>

// Only pointers and references to these are stored here, so the full
// headers are included in history_window.cpp instead.
class HistoryStore;
class HistoryTable;
class SmoothScroll;
class WrappedText;

// The window the clock button opens: the searches in a table, newest
// first, and under it the selected one in full, with its DeepL
// translation if there's one kept. It follows the history as it changes,
// open or not.
class HistoryWindow {
public:
    // history has to outlive the window
    explicit HistoryWindow(HistoryStore& history);
    // Stops listening to history
    ~HistoryWindow();

    // One history window per app: copying makes no sense
    HistoryWindow(const HistoryWindow&) = delete;
    HistoryWindow& operator=(const HistoryWindow&) = delete;

    // Opens the window, or raises it if it's already open
    void show();

private:
    HistoryStore& history;
    // From history.add_listener
    int history_listener = -1;

    // Owned by HistoryWindow as a member, so no new/delete anywhere.
    // Closing the window only hides it.
    Fl_Double_Window win{760, 540, "History"};

    // Created with new in the constructor and added to win, which deletes
    // them when it dies. details_text is details' only child.
    HistoryTable* table = nullptr;
    SmoothScroll* details = nullptr;
    WrappedText* details_text = nullptr;

    // Shows the history as it is now
    void refresh();
    // Shows the selected search in the details pane
    void show_details();
};
