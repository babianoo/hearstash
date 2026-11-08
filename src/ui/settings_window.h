#pragma once

#include <array>
#include <string>
#include <vector>

// FLTK
#include <FL/Fl_Double_Window.H>

#include "core/anki_fields.h"
#include "ui/global_hotkey.h"

// Only pointers and references to these are stored here, so the full
// headers are included in settings_window.cpp instead.
class Capturer;
class ClipboardInput;
class HistoryStore;
class ModelDownloader;
class SettingsStore;
class ShortcutInput;
class Fl_Box;
class Fl_Button;
class Fl_Check_Button;
class Fl_Choice;
class Fl_Group;
class Fl_Progress;
class Fl_Widget;
struct WhisperModel;

// The window the gear button opens: a sidebar of categories on the left,
// and on the right the page of the selected one. The History page has how
// many searches the history keeps, and a button that clears it. The API
// page has the DeepL key, and the Speech-to-Text page the whisper models,
// a row each to download, delete and choose one, then the microphone to
// record from. The Buffer page has the rolling buffer of what the computer
// plays: whether it records, how long, whether it's recording now, and
// the capture shortcut (on Linux, the command to bind to a key). The Anki page has the deck and
// note type to add cards to, which field gets each part of a card, both
// listed from Anki, and the picture's quality. The General page has the
// mouse wheel's scroll speed.
//
// Changes go into the SettingsStore it was given as they're made, which
// saves them: there's no Apply button.
class SettingsWindow {
public:
    // settings, history, downloader and capturer have to outlive the window
    SettingsWindow(SettingsStore& settings, HistoryStore& history, ModelDownloader& downloader,
                   Capturer& capturer);
    // Stops listening to history and downloader
    ~SettingsWindow();

    // One settings window per app: copying makes no sense
    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    // Opens the window, or raises it if it's already open. It opens on the
    // category that was selected last.
    void show();
    // The same, on the Speech-to-Text page, e.g. when there's no model to
    // transcribe with
    void show_speech_to_text();

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
    HistoryStore& history;
    ModelDownloader& downloader;
    Capturer& capturer;
    // From history.add_listener, downloader.add_listener and
    // settings.add_listener
    int history_listener = -1;
    int downloader_listener = -1;
    int settings_listener = -1;

    // Owned by SettingsWindow as a member, so no new/delete anywhere.
    // Closing the window only hides it, so its settings stay as they were.
    Fl_Double_Window win{600, 440, "Settings"};

    // Created with new in the constructor and added to win, which deletes
    // them when it dies. Both are in sidebar order: categories[i] shows
    // pages[i].
    std::vector<Fl_Button*> categories;
    // All in the same spot, right of the sidebar; only one is shown
    std::vector<Fl_Group*> pages;
    // On the General page. Applied like capacity_input.
    ClipboardInput* scroll_speed_input = nullptr;
    // On the History page. How many searches there are is shown next to
    // the button that clears them.
    ClipboardInput* capacity_input = nullptr;
    Fl_Button* clear_history_btn = nullptr;
    Fl_Box* history_count = nullptr;
    // On the API page. Starts hidden; key_visibility_btn toggles that.
    ClipboardInput* key_input = nullptr;
    Fl_Button* key_visibility_btn = nullptr;
    // On the Speech-to-Text page, indexed like WHISPER_MODELS
    std::vector<ModelRow> model_rows;
    // Also there: "Default", then the microphones. mic_names[i] is what
    // item i saves, "" for Default.
    Fl_Choice* mic_choice = nullptr;
    std::vector<std::string> mic_names;
    // On the Buffer page. The numbers are applied like capacity_input's.
    Fl_Check_Button* buffer_enabled = nullptr;
    ClipboardInput* buffer_seconds_input = nullptr;
    ClipboardInput* selection_seconds_input = nullptr;
    // Recording, off, or why it can't record
    Fl_Box* buffer_status = nullptr;
    // The capture shortcut, on Windows only
    ShortcutInput* shortcut_input = nullptr;
    // Why the capture shortcut can't be received, if it can't
    Fl_Box* hotkey_status = nullptr;
    // The same for the screen capture, on the Screen Text page
    ShortcutInput* screen_shortcut_input = nullptr;
    Fl_Box* screen_hotkey_status = nullptr;
    // On the Anki page. The names are what each item of the dropdowns
    // saves; a field's dropdown starts with "(none)", which saves "".
    Fl_Choice* deck_choice = nullptr;
    Fl_Choice* note_type_choice = nullptr;
    std::array<Fl_Choice*, CARD_PART_COUNT> field_choices{};
    std::vector<std::string> deck_names, note_type_names, field_names;
    // Applied like capacity_input
    ClipboardInput* quality_input = nullptr;
    // Whether Anki could be reached, or why not
    Fl_Box* anki_status = nullptr;
    // The lists have been asked from Anki since the window was opened
    bool anki_listed = false;

    // Static because FLTK callbacks are plain functions; the
    // SettingsWindow comes in through data.
    static void on_category(Fl_Widget* w, void* data);
    static void on_capacity(Fl_Widget* w, void* data);
    static void on_scroll_speed(Fl_Widget* w, void* data);
    // The window's close button and Escape
    static void on_close(Fl_Widget* w, void* data);
    static void on_clear_history(Fl_Widget* w, void* data);
    static void on_key_changed(Fl_Widget* w, void* data);
    static void on_key_visibility(Fl_Widget* w, void* data);
    static void on_model_chosen(Fl_Widget* w, void* data);
    static void on_model_action(Fl_Widget* w, void* data);
    static void on_mic_chosen(Fl_Widget* w, void* data);
    static void on_buffer_enabled(Fl_Widget* w, void* data);
    static void on_buffer_seconds(Fl_Widget* w, void* data);
    static void on_shortcut(Fl_Widget* w, void* data);
    static void on_screen_shortcut(Fl_Widget* w, void* data);
    static void on_deck_chosen(Fl_Widget* w, void* data);
    static void on_note_type_chosen(Fl_Widget* w, void* data);
    static void on_field_chosen(Fl_Widget* w, void* data);
    static void on_anki_reload(Fl_Widget* w, void* data);
    static void on_quality(Fl_Widget* w, void* data);

    // Add a page's settings to the current group, a page starting at x
    // that's w wide
    void add_general_settings(int x, int w);
    void add_history_settings(int x, int w);
    void add_api_settings(int x, int w);
    void add_speech_settings(int x, int w);
    void add_buffer_settings(int x, int w);
    void add_screen_text_settings(int x, int w);
    // A shortcut's box (Windows) or command (Linux) under its label, from
    // y, which is moved down past them, and a line for why it can't be
    // received
    void add_hotkey_settings(int left, int right, int& y, HotkeyAction action);
    void add_anki_settings(int x, int w);
    void select(size_t i);
    // Shows how many searches there are, which changes with every search
    void refresh_history();
    // Saves the number typed in capacity_input, and deletes the oldest
    // searches over it. Does nothing if it hasn't changed.
    void apply_capacity();
    // Saves the scroll speed typed. Does nothing if it hasn't changed.
    void apply_scroll_speed();
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
    // Saves the numbers typed on the Buffer page. Does nothing if they
    // haven't changed.
    void apply_buffer_seconds();
    // Shows whether the buffer is recording, which also changes when the
    // audio system comes or goes
    void refresh_buffer();
    // Lists the decks, note types and fields from Anki, with the saved
    // ones selected. If Anki can't be reached, only the saved ones are
    // listed, and anki_status says why.
    void refresh_anki();
    // Lists the saved note type's fields, as of the last refresh_anki()
    void refresh_fields();
    // Saves the picture quality typed. Does nothing if it hasn't changed.
    void apply_quality();
};
