#pragma once

#include <functional>

// FLTK
#include <FL/Fl_Double_Window.H>

// Only pointers and references to these are stored here, so the full
// headers are included in stash_window.cpp instead.
class SmoothScroll;
class StashRows;
class StashStore;
struct StashEntry;
class Fl_Box;
class Fl_Button;
class Fl_Widget;

// The window the stash button opens: the lines stashed to look at later,
// newest first, each with the screenshot taken with it, when, what it
// says if it was transcribed when stashed, in which app, and how long. It
// follows the stash as it changes, open or not.
//
// Double-clicking a line or pressing Enter opens it in the capture window,
// and Delete deletes it; the arrow keys move between them. Clear All
// deletes every one, after asking. Above the list it says why the last
// stash failed, if it did: the stash shortcut can't say so in a dialog.
class StashWindow {
public:
    // stash has to outlive the window. open is called to open a line.
    StashWindow(StashStore& stash, std::function<void(const StashEntry&)> open);
    // Stops listening to stash
    ~StashWindow();

    // One stash window per app: copying makes no sense
    StashWindow(const StashWindow&) = delete;
    StashWindow& operator=(const StashWindow&) = delete;

    // Opens the window, or raises it if it's already open
    void show();

private:
    StashStore& stash;
    std::function<void(const StashEntry&)> open;
    // From stash.add_listener
    int stash_listener = -1;

    // Owned by StashWindow as a member, so no new/delete anywhere.
    // Closing the window only hides it.
    Fl_Double_Window win{640, 520, "Stash"};

    // Created with new in the constructor and added to win, which deletes
    // them when it dies. rows is scroll's only child.
    Fl_Box* summary = nullptr;
    Fl_Box* error_box = nullptr;
    Fl_Button* open_btn = nullptr;
    Fl_Button* delete_btn = nullptr;
    Fl_Button* clear_btn = nullptr;
    SmoothScroll* scroll = nullptr;
    StashRows* rows = nullptr;

    // Static because FLTK callbacks are plain functions; the StashWindow
    // comes in through data.
    static void on_open(Fl_Widget* w, void* data);
    static void on_delete(Fl_Widget* w, void* data);
    static void on_clear(Fl_Widget* w, void* data);

    // Shows the stash as it is now
    void refresh();
    // Opens the selected line, if there's one
    void open_selected();
    // Deletes the selected line, if there's one
    void delete_selected();
    // Asks, then deletes every line
    void clear();
    // Makes the rows as wide as the scroll's content, and tall enough for
    // all of them
    void fit_rows();
};
