#pragma once

#include <memory>
#include <vector>

// FLTK
#include <FL/Fl_Double_Window.H>

// Only pointers and references to these are stored here, so the full
// headers are included in capture_window.cpp instead.
class ClipPlayer;
class Waveform;
class Fl_Box;
class Fl_Button;
class Fl_Widget;

// The window a capture opens in: the waveform of the last seconds of
// what the computer played, with the end already selected. The selection
// can be changed by dragging, and played back with Space or the Play
// button.
//
// Closing it (or Escape) hides it, stops playing, and forgets the audio.
class CaptureWindow {
public:
    CaptureWindow();
    // Defined in capture_window.cpp, where ClipPlayer is complete, which
    // unique_ptr needs to delete it
    ~CaptureWindow();

    // One capture window per app: copying makes no sense
    CaptureWindow(const CaptureWindow&) = delete;
    CaptureWindow& operator=(const CaptureWindow&) = delete;

    // Shows audio, rate a second, in place of the capture there was, with
    // the last selection_seconds of it selected, and brings the window to
    // the front
    void open(std::vector<float> audio, int rate, int selection_seconds);

private:
    // Owned by CaptureWindow as a member, so no new/delete anywhere
    Fl_Double_Window win{900, 560, "Capture"};

    // Created with new in the constructor and added to win, which deletes
    // them when it dies
    Waveform* waveform = nullptr;
    Fl_Button* play_btn = nullptr;
    // How long the selection is
    Fl_Box* selection_info = nullptr;

    // What waveform shows; it only borrows it
    std::vector<float> audio;
    int rate = 1;
    // Created on the first play, so a machine without audio output only
    // finds out then
    std::unique_ptr<ClipPlayer> player;

    // Static because FLTK callbacks are plain functions; the CaptureWindow
    // comes in through data.
    static void on_play(Fl_Widget* w, void* data);
    // The window's close button and Escape
    static void on_close(Fl_Widget* w, void* data);
    // Many times a second while playing: moves the playhead, and notices
    // the end
    static void on_play_tick(void* data);

    void play();
    void stop_playing();
    // Shows the selection's length, and whether there's one to act on
    void refresh_selection();
};
