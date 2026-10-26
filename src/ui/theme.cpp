#include "ui/theme.h"

// FLTK
#include <FL/Fl.H>

void init_theme(){
    Fl::scheme("gtk+");
}

Fl_Color sidebar_color(){
    return fl_color_average(FL_BACKGROUND_COLOR, FL_BLACK, 0.92f);
}
