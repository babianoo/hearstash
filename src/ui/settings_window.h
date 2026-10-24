#pragma once

// FLTK
#include <FL/Fl_Double_Window.H>

// The window the gear button opens. Empty for now: the settings (like the
// DeepL key) are still to come.
class SettingsWindow {
public:
    SettingsWindow();

    // One settings window per app: copying makes no sense
    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    // Opens the window, or raises it if it's already open
    void show();

private:
    // Owned by SettingsWindow as a member, so no new/delete anywhere.
    // Closing the window only hides it, so its settings stay as they were.
    Fl_Double_Window win{600, 400, "Settings"};
};
