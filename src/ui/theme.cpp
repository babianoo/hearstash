#include "ui/theme.h"

// FLTK
#include <FL/Fl.H>
#include <FL/Fl_Tooltip.H>

void init_theme(){
    Fl::scheme("gtk+");
    // White tooltips rather than FLTK's default yellow
    Fl_Tooltip::color(FL_WHITE);
}

Fl_Color sidebar_color(){
    return fl_color_average(FL_BACKGROUND_COLOR, FL_BLACK, 0.92f);
}
