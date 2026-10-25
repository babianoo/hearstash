#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// FLTK
#include <FL/Fl_Double_Window.H>

#include "core/jisho.h"
#include "core/transcriber.h"

// Only pointers and references to these are stored here, so the full
// headers are included in main_window.cpp instead.
class ModelDownloader;
class Recorder;
class SearchBar;
class SettingsStore;
class SettingsWindow;
class SmoothScroll;
class WrappedText;
class Fl_Widget;
struct TextSpan;

class MainWindow {
public:
    // settings and downloader have to outlive the window
    MainWindow(SettingsStore& settings, ModelDownloader& downloader);
    // Defined in main_window.cpp, where SettingsWindow and Recorder are
    // complete types, which unique_ptr needs to delete them. Stops a
    // transcription still running and waits for it.
    ~MainWindow();

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

    // What the transcription thread hands back to the UI thread
    struct Transcription {
        MainWindow* win;
        std::string text;
        std::string error{};    // Non-empty if it failed
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

    // Changed by settings_win, read when searching
    SettingsStore& settings;
    // Only passed on to settings_win
    ModelDownloader& downloader;
    // Created on the first click of the settings button. Not a plain
    // member: constructing a window makes it the current group, so while
    // win's widgets are being created it would take them in instead of win.
    std::unique_ptr<SettingsWindow> settings_win;

    // Bumped on every search. Only the latest search's result is shown,
    // so a slow earlier search can't overwrite a newer one.
    unsigned latest_search = 0;

    // Created the first time Record Input is picked, so a machine without
    // audio only finds out then
    std::unique_ptr<Recorder> recorder;
    // Keeps the chosen model loaded between recordings. Only the
    // transcription thread uses it, one at a time.
    Transcriber transcriber;
    // Joinable while transcribing. Declared after transcriber, so it's
    // destroyed first: a jthread requests stop and joins when it dies,
    // and the thread uses transcriber until then.
    std::jthread transcription_thread;

    // Static because FLTK callbacks are plain functions; the MainWindow
    // comes in through data.
    static void on_search(Fl_Widget* w, void* data);
    static void on_settings(Fl_Widget* w, void* data);
    // Runs on the UI thread, called by Fl::awake() from a search thread
    static void on_search_done(void* data);
    // A few times a second while recording: stops at the limit, and shows
    // how long it's been
    static void on_record_tick(void* data);
    // Runs on the UI thread, called by Fl::awake() from the transcription
    // thread
    static void on_transcribed(void* data);

    // Record Input was picked, or the red "+" clicked to stop
    void on_record();
    void start_recording();
    // Stops recording and starts transcribing what was recorded
    void stop_recording();
    // Created on first use, for the same reason as settings_win
    SettingsWindow& settings_window();
    void move_bar_to_top();
    // Fills the space under the search bar, which grows with long text
    void place_results();
    // Replaces what the results area shows, scrolled back to the top.
    // Plain text is in the default TextStyle.
    void show_text(std::string_view text);
    void show_text(const std::vector<TextSpan>& spans);
    void show_result(const SearchResult& result);
};
