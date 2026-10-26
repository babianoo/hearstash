#include "ui/category_button.h"

// FLTK
#include <FL/fl_draw.H>
#include <FL/Fl_Group.H>

#include "ui/theme.h"

// Width of the bar along the selected one's right edge
static constexpr int BAR_W = 3;

CategoryButton::CategoryButton(int x, int y, int w, int h, const char* label, int indent)
    : Fl_Button(x, y, w, h, label), indent(indent){
    type(FL_RADIO_BUTTON);
    clear_visible_focus();    // No dotted focus ring after a click
}

void CategoryButton::draw(){
    // The sidebar's color, so it looks like part of it
    draw_box(FL_FLAT_BOX, parent()->color());
    fl_font(value() ? FL_HELVETICA_BOLD : FL_HELVETICA, labelsize());
    fl_color(value() ? FL_FOREGROUND_COLOR : DIM);
    fl_draw(label(), x() + indent, y(), w() - indent, h(), FL_ALIGN_LEFT);
    if (value()){
        fl_color(ACCENT);
        fl_rectf(x() + w() - BAR_W, y(), BAR_W, h());
    }
}
