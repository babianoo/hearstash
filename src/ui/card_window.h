#pragma once

#include <string>
#include <vector>

// FLTK
#include <FL/Fl_Double_Window.H>

#include "core/image.h"

// Only pointers and references to these are stored here, so the full
// headers are included in card_window.cpp instead.
class ClipboardInput;
class CropView;
class SettingsStore;
class Fl_Box;
class Fl_Button;
class Fl_Widget;

// What a card starts with, from a capture
struct CardDraft {
    std::string word;
    std::string reading;
    std::string meaning;
    std::string sentence;
    // The selection in the capture window
    std::vector<float> audio;
    int rate = 0;
    // Taken when the shortcut was pressed; empty if it couldn't be, and
    // screenshot_error says why
    Image screenshot;
    std::string screenshot_error;
};

// The window "Add to Anki" opens: the card's text in fields that can be
// edited, and the screenshot with a rectangle to crop it to, which starts
// where the last card's crop was. The card is only added when Add is
// pressed, to the deck and note type chosen in Settings > Anki, each part
// in the field chosen there.
class CardWindow {
public:
    // settings has to outlive the window
    explicit CardWindow(SettingsStore& settings);

    // One card window per app: copying makes no sense
    CardWindow(const CardWindow&) = delete;
    CardWindow& operator=(const CardWindow&) = delete;

    // Shows draft in place of the card there was, and brings the window to
    // the front
    void open(CardDraft draft);

private:
    SettingsStore& settings;

    // Owned by CardWindow as a member, so no new/delete anywhere. Closing
    // it (or Escape) hides it.
    Fl_Double_Window win{720, 650, "Add to Anki"};

    // Created with new in the constructor and added to win, which deletes
    // them when it dies
    ClipboardInput* word_input = nullptr;
    ClipboardInput* reading_input = nullptr;
    ClipboardInput* meaning_input = nullptr;
    ClipboardInput* sentence_input = nullptr;
    // How long the audio is
    Fl_Box* audio_info = nullptr;
    CropView* crop_view = nullptr;
    // Where the card goes, or what's missing in the settings
    Fl_Box* destination = nullptr;
    Fl_Button* add_btn = nullptr;

    // What crop_view shows; it only borrows it
    CardDraft draft;

    // Static because FLTK callbacks are plain functions; the CardWindow
    // comes in through data.
    static void on_add(Fl_Widget* w, void* data);
    static void on_close(Fl_Widget* w, void* data);

    void add();
    // Hides the window, and forgets the card
    void close();
    // Puts the crop where the last card's was, if it fits this screenshot
    void restore_crop();
    // Remembers the crop for the next card
    void save_crop();
    // Shows where the card will go, as the settings are now
    void refresh_destination();
};
