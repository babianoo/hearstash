#pragma once

#include <string>
#include <vector>

// FLTK
#include <FL/Fl_Double_Window.H>

// Only pointers and references to these are stored here, so the full
// headers are included in settings_window.cpp instead.
class ClipboardInput;
class ModelDownloader;
class SettingsStore;
class Fl_Box;
class Fl_Button;
class Fl_Choice;
class Fl_Group;
class Fl_Progress;
class Fl_Widget;
struct WhisperModel;

// The window the gear button opens: a sidebar of categories on the left,
// and on the right the page of the selected one. The API page has the
// DeepL key, and the Speech-to-Text page the whisper models, a row each
// to download, delete and choose one, then the microphone to record
// from. The other pages are empty.
//
// Changes go into the SettingsStore it was given as they're made, which
// saves them: there's no Apply button.
class SettingsWindow {
public:
    // settings and downloader have to outlive the window
    SettingsWindow(SettingsStore& settings, ModelDownloader& downloader);
    // Stops listening to downloader
    ~SettingsWindow();

    // One settings window per app: copying makes no sense
    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    // Opens the window, or raises it if it's already open. It opens on the
    // category that was selected last.
    void show();

private:
    // A whisper model's row on the Speech-to-Text page, left to right
    struct ModelRow {
        // Radio button: chooses the model to transcribe with. Only active
        // when it's installed.
        Fl_Button* choose = nullptr;
        // The download size, or in its place while downloading, progress
        Fl_Box* size = nullptr;
        Fl_Progress* progress = nullptr;
        // Download, Cancel, Retry or Delete
        Fl_Button* action = nullptr;
        // As of the last refresh, to tell when a download has just finished
        bool downloading = false;
    };

    SettingsStore& settings;
    ModelDownloader& downloader;
    // From downloader.add_listener
    int downloader_listener = -1;

    // Owned by SettingsWindow as a member, so no new/delete anywhere.
    // Closing the window only hides it, so its settings stay as they were.
    Fl_Double_Window win{600, 400, "Settings"};

    // Created with new in the constructor and added to win, which deletes
    // them when it dies. Both are in sidebar order: categories[i] shows
    // pages[i].
    std::vector<Fl_Button*> categories;
    // All in the same spot, right of the sidebar; only one is shown
    std::vector<Fl_Group*> pages;
    // On the API page. Starts hidden; key_visibility_btn toggles that.
    ClipboardInput* key_input = nullptr;
    Fl_Button* key_visibility_btn = nullptr;
    // On the Speech-to-Text page, indexed like WHISPER_MODELS
    std::vector<ModelRow> model_rows;
    // Also there: "Default", then the microphones. mic_names[i] is what
    // item i saves, "" for Default.
    Fl_Choice* mic_choice = nullptr;
    std::vector<std::string> mic_names;

    // Static because FLTK callbacks are plain functions; the
    // SettingsWindow comes in through data.
    static void on_category(Fl_Widget* w, void* data);
    static void on_key_changed(Fl_Widget* w, void* data);
    static void on_key_visibility(Fl_Widget* w, void* data);
    static void on_model_chosen(Fl_Widget* w, void* data);
    static void on_model_action(Fl_Widget* w, void* data);
    static void on_mic_chosen(Fl_Widget* w, void* data);

    // Add a page's settings to the current group, a page starting at x
    // that's w wide
    void add_api_settings(int x, int w);
    void add_speech_settings(int x, int w);
    void select(size_t i);
    // Hides or shows the key, and switches the button's icon to match
    void set_key_hidden(bool hidden);
    // Where in model_rows the row with widget w is
    size_t model_row_of(const Fl_Widget* w) const;
    // Makes model's row show its download and whether it's installed and
    // chosen, which can change outside the window too
    void refresh_model(const WhisperModel& model);
    void refresh_models();
    void delete_model(const WhisperModel& model);
    // Lists the microphones plugged in now, with the saved one selected
    void refresh_microphones();
};
