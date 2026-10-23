#pragma once

#include <string>
#include <vector>

// FLTK
#include <FL/Fl_Widget.H>

// How a piece of text looks
struct TextStyle {
    Fl_Font font = FL_HELVETICA;    // FL_HELVETICA_BOLD, FL_HELVETICA_ITALIC...
    Fl_Fontsize size = FL_NORMAL_SIZE;
    Fl_Color color = FL_FOREGROUND_COLOR;
};

// A piece of text in one style. May contain '\n' and span several lines.
struct TextSpan {
    std::string text;
    TextStyle style{};
};

// Styled text that wraps to the widget's width and sets its own height to
// fit all of it, so it can sit inside a SmoothScroll.
//
// Lines wrap at spaces, or anywhere for long runs without spaces (like
// Japanese). Wrapped lines keep the line's indent, plus the "1. " of a
// numbered line. Unlike labels, '@' and '&' are drawn as is. A line is as
// tall as its biggest text, and all its text sits on one baseline.
//
// The text can be selected by dragging over it, like in a text field.
// Releasing the mouse copies the selection for middle-click pasting, and
// Ctrl+C copies it to the clipboard. Ctrl+A selects everything.
//
// Uses selection_color() behind the selected part.
class WrappedText : public Fl_Widget {
public:
    // Starts with no text, so only as tall as its padding
    WrappedText(int x, int y, int w);

    // Replaces the text with the spans, one after another. Re-wraps and
    // resizes. Needs the display to be open (a window shown), because
    // wrapping measures the text.
    void text(const std::vector<TextSpan>& spans);

    // Re-wraps when the width changes
    void resize(int x, int y, int w, int h) override;

    int handle(int event) override;

protected:
    void draw() override;

private:
    // Styles full_text from where the previous run ended up to end. The
    // last run ends at full_text.size(); there's always at least one.
    struct Run {
        size_t end;
        TextStyle style;
    };

    // One line on screen: full_text from start up to end, drawn indent
    // pixels in from the left padding
    struct Line {
        size_t start = 0, end = 0;
        int indent = 0;
        // Top of the line, from below the top padding
        int y = 0;
        int h = 0;
        // From the line's top
        int baseline = 0;
    };

    // All the spans' text, one after another
    std::string full_text;
    std::vector<Run> runs;
    // full_text split into lines that fit the width
    std::vector<Line> lines;

    // The selection, as byte offsets into full_text: where the drag
    // started and where the mouse is now. Equal when nothing's selected.
    size_t sel_anchor = 0, sel_cursor = 0;

    void rewrap();

    // Index of the run that styles the byte at offset (the last run for
    // the end of the text)
    size_t run_at(size_t offset) const;
    // Pixel width of full_text from from up to to, each part in its style
    double width(size_t from, size_t to) const;
    // End of what fits in max_w pixels, starting at from and stopping by to
    size_t fit(size_t from, size_t to, double max_w) const;
    // Sets line's height and baseline from the fonts on it
    void measure(Line& line) const;
    // Draws line's text starting at x, in its own colors or in colors that
    // are readable on the selection highlight
    void draw_line(const Line& line, int x, int baseline, bool on_selection) const;

    // Offset in full_text of the character boundary nearest to the point
    size_t offset_at(int mouse_x, int mouse_y);

    // Copies the selection: to the middle-click buffer (0) or clipboard (1)
    void copy_selection(int destination) const;
};
