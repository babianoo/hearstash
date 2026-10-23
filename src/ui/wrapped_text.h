#pragma once

#include <string>
#include <string_view>
#include <vector>

// FLTK
#include <FL/Fl_Widget.H>

// Plain text that wraps to the widget's width and sets its own height to
// fit all of it, so it can sit inside a SmoothScroll.
//
// Lines wrap at spaces, or anywhere for long runs without spaces (like
// Japanese). Wrapped lines keep the line's indent, plus the "1. " of a
// numbered line. Unlike labels, '@' and '&' are drawn as is.
//
// The text can be selected by dragging over it, like in a text field.
// Releasing the mouse copies the selection for middle-click pasting, and
// Ctrl+C copies it to the clipboard. Ctrl+A selects everything.
//
// Uses labelfont(), labelsize() and labelcolor() for the text, and
// selection_color() behind the selected part.
class WrappedText : public Fl_Widget {
public:
    // Starts with no text, so only as tall as its padding
    WrappedText(int x, int y, int w);

    // Re-wraps and resizes. Needs the display to be open (a window shown),
    // because wrapping measures the text.
    void text(std::string_view new_text);

    // Re-wraps when the width changes
    void resize(int x, int y, int w, int h) override;

    int handle(int event) override;

protected:
    void draw() override;

private:
    // One line on screen, drawn indent pixels in from the left padding
    struct Line {
        std::string text;
        int indent = 0;
        // Where text starts in full_text
        size_t start = 0;
    };

    std::string full_text;
    // full_text split into lines that fit the width
    std::vector<Line> lines;

    // The selection, as byte offsets into full_text: where the drag
    // started and where the mouse is now. Equal when nothing's selected.
    size_t sel_anchor = 0, sel_cursor = 0;

    void rewrap();

    // Offset in full_text of the character boundary nearest to the point
    size_t offset_at(int mouse_x, int mouse_y);

    // Copies the selection: to the middle-click buffer (0) or clipboard (1)
    void copy_selection(int destination) const;
};
