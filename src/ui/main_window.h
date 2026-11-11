#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// FLTK
#include <FL/Fl_Double_Window.H>

#include "core/api.h"
#include "core/jisho.h"

// Only pointers and references to these are stored here, so the full
// headers are included in main_window.cpp instead.
class Capturer;
class HistoryStore;
class HistoryWindow;
class ModelDownloader;
class Recorder;
class SearchBar;
class SettingsStore;
class SettingsWindow;
class SharedTranscriber;
class SmoothScroll;
class StashStore;
class StashWindow;
class WrappedText;
class Fl_Button;
class Fl_Widget;
struct TextSpan;

class MainWindow {
public:
    // settings, history, downloader, capturer, transcriber and stash have
    // to outlive the window
    MainWindow(SettingsStore& settings, HistoryStore& history, ModelDownloader& downloader, Capturer& capturer,
               SharedTranscriber& transcriber, StashStore& stash);
    // Defined in main_window.cpp, where SettingsWindow, HistoryWindow,
    // StashWindow and Recorder are complete types, which unique_ptr needs
    // to delete them. Stops listening to the stash.
    // Stops a transcription still running and waits for it, or for the
    // model to finish loading.
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
        Api api;
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
    Fl_Double_Window win{900, 600, "Hearstash"};

    // Created with new in the constructor and added to win, which
    // deletes them when it dies. MainWindow only keeps a handle to them.
    SearchBar* search_bar = nullptr;
    // Placeholder until the results UI is designed: one block of text in
    // a scroll. results_text is results' only child.
    SmoothScroll* results = nullptr;
    WrappedText* results_text = nullptr;

    // Changed by settings_win, read when searching
    SettingsStore& settings;
    // What was searched. Searches are added as their results come in.
    HistoryStore& history;
    // Only passed on to settings_win
    ModelDownloader& downloader;
    Capturer& capturer;
    // Created on the first click of the settings button. Not a plain
    // member: constructing a window makes it the current group, so while
    // win's widgets are being created it would take them in instead of win.
    std::unique_ptr<SettingsWindow> settings_win;
    // Created on the first click of the history button, for the same
    // reason
    std::unique_ptr<HistoryWindow> history_win;
    // Stashed lines, which the stash button counts
    StashStore& stash;
    // From stash.add_listener
    int stash_listener = -1;
    // Its child of win, and its tooltip, which it only borrows
    Fl_Button* stash_btn = nullptr;
    std::string stash_tooltip;
    // Created on the first click of the stash button, for the same reason
    std::unique_ptr<StashWindow> stash_win;

    // Bumped on every search. Only the latest search's result is shown,
    // so a slow earlier search can't overwrite a newer one.
    unsigned latest_search = 0;

    // Created the first time Record Input is picked, so a machine without
    // audio only finds out then
    std::unique_ptr<Recorder> recorder;
    // Held from the start of a recording until its text comes in, so the
    // model stays loaded meanwhile
    SharedTranscriber& transcriber;
    // What was recorded, handed from the UI thread to the transcription
    // thread, which waits for it once the model is loaded
    std::mutex recorded_mutex;
    std::condition_variable_any recorded_ready;
    std::optional<std::vector<float>> recorded;
    // Joinable from the start of a recording until its text comes in.
    // Declared after what it uses, so it's destroyed first: a jthread
    // requests stop and joins when it dies.
    std::jthread transcription_thread;

    // Static because FLTK callbacks are plain functions; the MainWindow
    // comes in through data.
    static void on_search(Fl_Widget* w, void* data);
    static void on_settings(Fl_Widget* w, void* data);
    static void on_history(Fl_Widget* w, void* data);
    static void on_stash(Fl_Widget* w, void* data);
    // The window's close button: quits the app. Escape does nothing.
    static void on_close(Fl_Widget* w, void* data);
    // Runs on the UI thread, called by Fl::awake() from a search thread
    static void on_search_done(void* data);
    // A few times a second while recording: stops at the limit, and shows
    // how long it's been
    static void on_record_tick(void* data);
    // Runs on the UI thread, called by Fl::awake() from the transcription
    // thread
    static void on_transcribed(void* data);

    // Searches for what's in the search bar, with its API
    void search();
    // Saves the search bar's API as the one to start with next time
    void remember_api();
    // Puts query and api in the search bar, searches, and brings the
    // window to the front. For a search picked in the history window.
    void search_again(const std::string& query, Api api);
    // Record Input was picked, or the red "+" clicked to stop
    void on_record();
    // Starts recording, and loading the model meanwhile
    void start_recording();
    // Stops recording and hands what was recorded to be transcribed
    void stop_recording();
    // Created on first use, for the same reason as settings_win
    SettingsWindow& settings_window();
    HistoryWindow& history_window();
    StashWindow& stash_window();
    // Shows how many lines are stashed on the stash button
    void refresh_stash_button();
    void move_bar_to_top();
    // Fills the space under the search bar, which grows with long text
    void place_results();
    // Replaces what the results area shows, scrolled back to the top.
    // Plain text is in the default TextStyle.
    void show_text(std::string_view text);
    void show_text(const std::vector<TextSpan>& spans);
    void show_result(const SearchResult& result);
};
