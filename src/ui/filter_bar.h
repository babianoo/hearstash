#pragma once

#include <functional>
#include <string>

// FLTK
#include <FL/Fl_Group.H>

// Only pointers to these are stored here, so the full headers are
// included in filter_bar.cpp instead.
class Fl_Box;
// The text box. Defined in filter_bar.cpp, since only FilterBar uses it.
class FilterInput;

// A pill-shaped text box like the main search bar's, PILL_H tall, with a
// magnifying glass in front and a dim placeholder while it's empty. It's
// always one line: line breaks that are pasted in become spaces.
//
// Its callback runs on every change to the text, so what it filters can
// follow as it's typed. Escape clears the text, or does what it would
// anyway (closing the window) when there's none.
class FilterBar : public Fl_Group {
public:
    // placeholder isn't copied, so it has to stay valid
    FilterBar(int x, int y, int w, const char* placeholder);

    std::string text() const;

    // listener gets Up, Down, Page Up, Page Down and Enter while the text
    // box has focus, e.g. to move through a list of what's filtered
    // without leaving the box. It returns whether it took the key; if not,
    // the text box gets it.
    void on_key(std::function<bool(int key)> listener);

    // Keeps the magnifying glass its size. Fl_Group's own resize() would
    // stretch it.
    void resize(int x, int y, int w, int h) override;

private:
    // Created with new in the constructor and added to this group, which
    // deletes them when it dies. FilterBar only keeps a handle to them.
    Fl_Box* icon = nullptr;
    FilterInput* input = nullptr;

    void place_children();

    // Static because FLTK callbacks are plain functions; the FilterBar
    // comes in through data.
    static void on_input(Fl_Widget* w, void* data);
};
