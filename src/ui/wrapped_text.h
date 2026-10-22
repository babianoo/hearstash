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
// Uses labelfont(), labelsize() and labelcolor() for the text.
class WrappedText : public Fl_Widget {
public:
    // Starts with no text, so only as tall as its padding
    WrappedText(int x, int y, int w);

    // Re-wraps and resizes. Needs the display to be open (a window shown),
    // because wrapping measures the text.
    void text(std::string_view new_text);

    // Re-wraps when the width changes
    void resize(int x, int y, int w, int h) override;

protected:
    void draw() override;

private:
    // One line on screen, drawn indent pixels in from the left padding
    struct Line {
        std::string text;
        int indent = 0;
    };

    std::string full_text;
    // full_text split into lines that fit the width
    std::vector<Line> lines;

    void rewrap();
};
