#include "ui/theme.h"

// FLTK
#include <FL/Fl.H>

void init_theme(){
    Fl::scheme("gtk+");

    // FL_ROUND_UP_BOX only insets text by 2px, so the cursor sits inside the
    // pill's curved end. Same drawing, but inset left/right by the radius.
    // get_boxtype() returns the current scheme's version, so this has to
    // come after Fl::scheme().
    Fl::set_boxtype(PILL_BOX, Fl::get_boxtype(FL_ROUND_UP_BOX), PILL_H / 2, 2, PILL_H, 4);
}
