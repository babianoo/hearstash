#pragma once

// FLTK
#include <FL/Fl_Input.H>

// Fl_Input whose copy, cut and paste go through ui/clipboard.h instead of
// FLTK's own clipboard, which loses what's copied on Wayland. Same keys
// and right-click menu as Fl_Input, whose versions can't be overridden.
//
// With type(FL_SECRET_INPUT) it hides its text like Fl_Secret_Input, and
// like it never copies: Cut and Copy do nothing, so the hidden text can't
// leak out through the clipboard.
class ClipboardInput : public Fl_Input {
public:
    using Fl_Input::Fl_Input;

    int handle(int event) override;

private:
    bool is_secret() const { return input_type() == FL_SECRET_INPUT; }

    void copy_selection();

    // Like Fl_Input's: a click outside the selection first selects the
    // word (or run of spaces) under the mouse, or moves the cursor to the
    // end when it's past the text
    void show_menu();
};
