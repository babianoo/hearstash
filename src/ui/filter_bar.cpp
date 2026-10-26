#include "ui/filter_bar.h"

#include <algorithm>

// FLTK
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/fl_draw.H>
#include <FL/Fl_SVG_Image.H>

#include "ui/clipboard_input.h"
#include "ui/icons.h"
#include "ui/theme.h"

static constexpr int ICON_SIZE = 16;
// From the pill's left end to the icon, and from the icon to the text
static constexpr int ICON_PAD = 10, ICON_GAP = 6;
// From the text to the pill's right end, past its rounding
static constexpr int RIGHT_PAD = 14;
// Above and below the text box, inside the pill's border
static constexpr int INPUT_PAD = 4;

// The text box, which shows the placeholder while it's empty
class FilterInput : public ClipboardInput {
public:
    FilterInput(int x, int y, int w, int h, const char* placeholder)
        : ClipboardInput(x, y, w, h), placeholder(placeholder){}

    void draw() override {
        ClipboardInput::draw();
        if (size() > 0) return;
        fl_font(textfont(), textsize());
        // Lighter than DIM: it's not text that's there
        fl_color(fl_color_average(FL_FOREGROUND_COLOR, color(), 0.45f));
        // Where Fl_Input starts its text: 3px into the box
        fl_draw(placeholder, x() + Fl::box_dx(box()) + 3, y(), w(), h(), FL_ALIGN_LEFT);
    }

private:
    const char* placeholder;
};

FilterBar::FilterBar(int x, int y, int w, const char* placeholder) : Fl_Group(x, y, w, PILL_H){
    // The pill is the group's own box, with the icon and text box inside
    // it, like the main search bar
    box(FL_ROUND_UP_BOX);
    color(FL_BACKGROUND2_COLOR);    // Same white as the text box's fill

    icon = new Fl_Box(0, 0, ICON_SIZE, ICON_SIZE);
    // Borrowed by the box, never freed, like the round buttons' icons
    auto* glass = new Fl_SVG_Image(nullptr, ICON_SEARCH);
    glass->scale(ICON_SIZE, ICON_SIZE);
    icon->image(glass);

    input = new FilterInput(0, 0, 1, 1, placeholder);
    input->box(FL_FLAT_BOX);
    input->when(FL_WHEN_CHANGED);
    input->callback(on_input, this);

    end();
    place_children();
}

std::string FilterBar::text() const {
    return input->value();
}

void FilterBar::resize(int x, int y, int w, int h){
    Fl_Widget::resize(x, y, w, h);
    place_children();
}

void FilterBar::place_children(){
    icon->resize(x() + ICON_PAD, y() + (h() - ICON_SIZE) / 2, ICON_SIZE, ICON_SIZE);
    const int input_x = icon->x() + ICON_SIZE + ICON_GAP;
    input->resize(input_x, y() + INPUT_PAD, std::max(1, x() + w() - RIGHT_PAD - input_x), h() - 2 * INPUT_PAD);
}

void FilterBar::on_input(Fl_Widget*, void* data){
    FilterBar* self = static_cast<FilterBar*>(data);
    Fl_Input* input = self->input;

    // A line break pasted in would show as ^J. Replaced in place, so the
    // text's length and the cursor stay where they are.
    std::string text = input->value();
    if (text.find_first_of("\r\n\t") != std::string::npos){
        std::ranges::replace_if(text, [](char c){ return c == '\r' || c == '\n' || c == '\t'; }, ' ');
        const int cursor = input->insert_position();
        input->value(text.c_str());
        input->insert_position(cursor);
    }

    self->do_callback();
}
