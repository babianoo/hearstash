#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// FLTK
#include <FL/Fl_Double_Window.H>

#include "core/jisho.h"

// Only pointers to these are stored here, so the full headers are
// included in main_window.cpp instead.
class SearchBar;
class SmoothScroll;
class WrappedText;
class Fl_Widget;
struct TextSpan;

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
        std::vector<JishoEntry> entries{};          // Jisho
        std::optional<std::string> translation{};   // DeepL
        std::string error{};    // Non-empty if the search failed
    };

    // Owned by MainWindow as a member, so no new/delete anywhere.
    // When MainWindow dies, win dies and deletes all of its children.
    Fl_Double_Window win{900, 600, "Prototype"};

    // Created with new in the constructor and added to win, which
    // deletes them when it dies. MainWindow only keeps a handle to them.
    SearchBar* search_bar = nullptr;
    // Placeholder until the results UI is designed: one block of text in
    // a scroll. results_text is results' only child.
    SmoothScroll* results = nullptr;
    WrappedText* results_text = nullptr;

    // Bumped on every search. Only the latest search's result is shown,
    // so a slow earlier search can't overwrite a newer one.
    unsigned latest_search = 0;

    // Static because FLTK callbacks are plain functions; the MainWindow
    // comes in through data.
    static void on_search(Fl_Widget* w, void* data);
    // Runs on the UI thread, called by Fl::awake() from a search thread
    static void on_search_done(void* data);

    void move_bar_to_top();
    // Replaces what the results area shows, scrolled back to the top.
    // Plain text is in the default TextStyle.
    void show_text(std::string_view text);
    void show_text(const std::vector<TextSpan>& spans);
    void show_result(const SearchResult& result);
};
