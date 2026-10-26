#pragma once

// FLTK
#include <FL/Fl_Button.H>

// An entry in a window's sidebar: plain text, no button look. The selected
// one is bold, with an accent bar along its right edge.
//
// A radio button, so selecting one deselects the others in its group. Its
// callback only runs when the selection changes: clicking the one that's
// already selected does nothing.
class CategoryButton : public Fl_Button {
public:
    // The label starts indent pixels in from the left edge
    CategoryButton(int x, int y, int w, int h, const char* label, int indent);

    void draw() override;

private:
    int indent;
};
