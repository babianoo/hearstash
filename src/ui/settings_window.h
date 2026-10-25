#pragma once

#include <vector>

// FLTK
#include <FL/Fl_Double_Window.H>

// Only pointers and references to these are stored here, so the full
// headers are included in settings_window.cpp instead.
class ClipboardInput;
class ModelDownloader;
class SettingsStore;
class Fl_Button;
class Fl_Group;
class Fl_Widget;

// The window the gear button opens: a sidebar of categories on the left,
// and on the right the page of the selected one. Only the API page has
// settings so far (the DeepL key); the others are empty.
//
// Changes go into the SettingsStore it was given as they're made, which
// saves them: there's no Apply button.
class SettingsWindow {
public:
    // settings and downloader have to outlive the window
    SettingsWindow(SettingsStore& settings, ModelDownloader& downloader);

    // One settings window per app: copying makes no sense
    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    // Opens the window, or raises it if it's already open. It opens on the
    // category that was selected last.
    void show();

private:
    SettingsStore& settings;
    // For the Speech-to-Text page, which isn't built yet
    [[maybe_unused]] ModelDownloader& downloader;

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

    // Static because FLTK callbacks are plain functions; the
    // SettingsWindow comes in through data.
    static void on_category(Fl_Widget* w, void* data);
    static void on_key_changed(Fl_Widget* w, void* data);
    static void on_key_visibility(Fl_Widget* w, void* data);

    // Adds the API page's settings to the current group, a page starting
    // at x that's w wide
    void add_api_settings(int x, int w);
    void select(size_t i);
    // Hides or shows the key, and switches the button's icon to match
    void set_key_hidden(bool hidden);
};
