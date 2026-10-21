#pragma once

#include <memory>
#include <string>
#include <vector>

// FLTK
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Text_Buffer.H>

#include "core/jisho.h"

// Only pointers to these are stored here, so the full headers are
// included in main_window.cpp instead.
class SearchBar;
class Fl_Text_Display;
class Fl_Widget;

class MainWindow {
public:
    MainWindow();

    // One app, one window: copying makes no sense
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void show(int argc, char** argv);

private:
    // What a search thread hands back to the UI thread
    struct SearchResult {
        MainWindow* win;
        unsigned id;
        std::string keyword;
        std::vector<JishoEntry> entries{};
        std::string error{};    // Non-empty if the search failed
    };

    // Declared before win so it's destroyed after it: the text display
    // (a child of win) still uses the buffer while it's being deleted.
    Fl_Text_Buffer results_buf;

    // Owned by MainWindow as a member, so no new/delete anywhere.
    // When MainWindow dies, win dies and deletes all of its children.
    Fl_Double_Window win{900, 600, "Prototype"};

    // Created with new in the constructor and added to win, which
    // deletes them when it dies. MainWindow only keeps a handle to them.
    SearchBar* search_bar = nullptr;
    // Placeholder until the results UI is designed
    Fl_Text_Display* results = nullptr;

    // Bumped on every search. Only the latest search's result is shown,
    // so a slow earlier search can't overwrite a newer one.
    unsigned latest_search = 0;

    // Static because FLTK callbacks are plain functions; the MainWindow
    // comes in through data.
    static void on_search(Fl_Widget* w, void* data);
    // Runs on the UI thread, called by Fl::awake() from a search thread
    static void on_search_done(void* data);

    void show_result(const SearchResult& result);
};
