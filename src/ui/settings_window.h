#pragma once

#include <vector>

// FLTK
#include <FL/Fl_Double_Window.H>

// Only pointers to these are stored here, so the full headers are
// included in settings_window.cpp instead.
class Fl_Button;
class Fl_Group;
class Fl_Widget;

// The window the gear button opens: a sidebar of categories on the left,
// and on the right the page of the selected one. The pages only have a
// heading so far: the settings themselves (like the DeepL key) are still
// to come.
class SettingsWindow {
public:
    SettingsWindow();

    // One settings window per app: copying makes no sense
    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    // Opens the window, or raises it if it's already open. It opens on the
    // category that was selected last.
    void show();

private:
    // Owned by SettingsWindow as a member, so no new/delete anywhere.
    // Closing the window only hides it, so its settings stay as they were.
    Fl_Double_Window win{600, 400, "Settings"};

    // Created with new in the constructor and added to win, which deletes
    // them when it dies. Both are in sidebar order: categories[i] shows
    // pages[i].
    std::vector<Fl_Button*> categories;
    // All in the same spot, right of the sidebar; only one is shown
    std::vector<Fl_Group*> pages;

    // Static because FLTK callbacks are plain functions; the
    // SettingsWindow comes in through data.
    static void on_category(Fl_Widget* w, void* data);

    void select(size_t i);
};
