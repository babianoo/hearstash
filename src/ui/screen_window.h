#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// FLTK
#include <FL/Fl_Double_Window.H>

#include "core/image.h"

// Only pointers and references to these are stored here, so the full
// headers are included in screen_window.cpp instead.
class CardWindow;
class CropView;
class LookupPanels;
class SettingsStore;
class TextReader;
class Fl_Box;
class Fl_Button;
class Fl_Widget;

// The window the screen shortcut opens (hearstash --read-screen), for
// games and visual novels: the capture window's twin, with the screenshot
// taken then where the waveform is. The text on it is read (OCR) as the
// window opens, and shows where the transcript would, to look up,
// translate and add to Anki the same way. It's translated by itself too,
// if the settings say so.
//
// What's read is the part of the screenshot in the rectangle on it: all
// of it at first. Dragging a new one, e.g. around the text box, reads
// that, and it's kept for the game (the program that had the focus), so
// the next capture there reads only it too. "Whole screen" goes back to
// all of it. Enter reads again.
//
// The models that read it are downloaded the first time, about 30 MB.
//
// Closing it (or Escape) hides it and forgets the screenshot.
class ScreenWindow {
public:
    // settings has to outlive the window
    explicit ScreenWindow(SettingsStore& settings);
    // Defined in screen_window.cpp, where the classes in unique_ptrs are
    // complete. Stops the reading still running and waits for it.
    ~ScreenWindow();

    // One screen window per app: copying makes no sense
    ScreenWindow(const ScreenWindow&) = delete;
    ScreenWindow& operator=(const ScreenWindow&) = delete;

    // Shows screenshot in place of the one there was, reads it, and brings
    // the window to the front. app is the program that had the focus
    // (focused_app()), for the part of the screen kept for it.
    void open(Image screenshot, std::string app);

private:
    // A reading, on a thread of its own. Each one hands its result back
    // through Fl::awake(), even when stopped, so it can be joined then.
    struct Job {
        unsigned id;
        std::jthread thread;
    };
    // What a reading hands back to the UI thread
    struct ReadResult {
        ScreenWindow* win;
        unsigned id;
        std::string text{};
        std::string error{};    // Non-empty if it failed
    };

    SettingsStore& settings;

    // Owned by ScreenWindow as a member, so no new/delete anywhere
    // Taller than the capture window, for the screenshot; the text below
    // is about as tall as there. Fits a 1080p screen.
    Fl_Double_Window win{900, 860, "Screen"};

    // Created with new in the constructor and added to win, which deletes
    // them when it dies
    CropView* crop_view = nullptr;
    Fl_Button* whole_btn = nullptr;
    // What's read, and for which game it's kept
    Fl_Box* area_info = nullptr;
    Fl_Button* translate_btn = nullptr;
    Fl_Button* anki_btn = nullptr;
    Fl_Button* read_btn = nullptr;
    // The text read and its lookups. Its widgets are win's.
    std::unique_ptr<LookupPanels> panels;

    // What crop_view shows; it only borrows it
    Image screenshot;
    std::string app;
    // Created the first time a card is added, like the capture window's
    std::unique_ptr<CardWindow> card_win;

    // Loaded by the first reading, and kept. Only used while reader_mutex
    // is held: readings take turns, and the models are downloaded under
    // it too. Before jobs, so it outlives their threads.
    std::mutex reader_mutex;
    std::unique_ptr<TextReader> reader;

    // Running, or done and not joined yet. Usually one.
    std::vector<Job> jobs;
    unsigned next_job_id = 1;
    // The reading whose text is awaited, or 0 if none
    unsigned reading = 0;

    // Static because FLTK callbacks are plain functions; the ScreenWindow
    // comes in through data.
    static void on_whole(Fl_Widget* w, void* data);
    static void on_read(Fl_Widget* w, void* data);
    static void on_translate(Fl_Widget* w, void* data);
    static void on_add_to_anki(Fl_Widget* w, void* data);
    // The window's close button and Escape
    static void on_close(Fl_Widget* w, void* data);
    // Runs on the UI thread, called by Fl::awake() from a reading
    static void on_read_done(void* data);

    // Reads the text in the rectangle
    void read();
    // Stops waiting for the text being read, if any
    void cancel_reading();
    // Keeps the rectangle for app, or forgets it when it's all of it
    void save_area();
    // Says what's read, and for which game it's kept
    void refresh_area_info();
    // Shows the buttons as they should be now
    void refresh_buttons();
    // Opens the card window with what's shown
    void add_to_anki();
};
