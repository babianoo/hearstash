#include "ui/search_bar.h"

#include <iterator>

// FLTK
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Multi_Label.H>
#include <FL/Fl_SVG_Image.H>

#include "ui/clipboard_input.h"
#include "ui/icons.h"
#include "ui/theme.h"

// Indexed by Api
static constexpr const char* API_NAMES[] = {"Jisho", "DeepL", "MyMemory"};

SearchBar::SearchBar(int x, int y, int w) : Fl_Group(x, y, w, PILL_H){
    // Fl_Group's constructor calls begin(), so widgets created below are
    // added to this group. Their positions are still window coordinates,
    // which is why everything is offset by x and y.
    const int btn_w = 80, h = PILL_H, pad = 3;

    // FLTK doesn't handle overlapping widgets: when the input redraws (e.g.
    // on focus) it would paint over a button placed on top of it. So the
    // pill is just the group's own box, drawn behind its children, and the
    // input and button sit side by side inside it. The input ending before
    // the button also means typed text scrolls instead of disappearing
    // under the button.
    box(FL_ROUND_UP_BOX);
    color(FL_BACKGROUND2_COLOR);    // Same white as the input's fill

    // API button in the pill's left end: a circle as tall as the Search
    // button, and concentric with the pill's curve.
    const int circle_d = h - 2 * pad;
    const int circle_x = x + pad;

    // Faint divider between the API button and the input
    const int divider_x = circle_x + circle_d + 4;

    // Starts a little after the circle and stops a little before the
    // button. Created before the button so it's the widget that gets
    // keyboard focus when the window opens.
    const int btn_x = x + w - btn_w - pad;
    const int input_x = divider_x + 1 + 6;
    input = new ClipboardInput(input_x, y + 2, btn_x - 6 - input_x, h - 4);
    input->box(FL_FLAT_BOX);
    input->when(FL_WHEN_ENTER_KEY_ALWAYS);    // Enter searches, even unchanged
    input->callback(on_submit, this);

    search_btn = new Fl_Button(btn_x, y + pad, btn_w, h - 2 * pad, "Search");
    search_btn->box(FL_ROUND_UP_BOX);
    search_btn->callback(on_submit, this);

    // 1px line, 85% of the way from the text color to the input's white,
    // so it's barely visible.
    Fl_Box* divider = new Fl_Box(divider_x, y + 8, 1, h - 16);
    divider->box(FL_FLAT_BOX);
    divider->color(fl_color_average(FL_BACKGROUND2_COLOR, FL_FOREGROUND_COLOR, 0.85f));

    // "+" that opens the menu (Record Input, API). No box, so the pill shows through; it
    // only draws a round grey circle while pressed. (A white FL_FLAT_BOX
    // would be square and cover the pill's curved edge.) The tooltip shows
    // the current API.
    api_btn = new Fl_Button(circle_x, y + pad, circle_d, circle_d, "+");
    api_btn->box(FL_NO_BOX);
    api_btn->down_box(FL_ROUND_DOWN_BOX);
    api_btn->tooltip(API_NAMES[static_cast<int>(current_api)]);
    api_btn->clear_visible_focus();      // No dotted focus ring after a click
    api_btn->callback(on_api_btn, this);

    end();
}

const char* SearchBar::text() const {
    return input->value();
}

void SearchBar::on_api_btn(Fl_Widget* w, void* data){
    SearchBar* self = static_cast<SearchBar*>(data);
    const int current = static_cast<int>(self->current_api);

    // Record Input, a divider, then one item per API.
    // Rebuilt on every click so the radio check follows the current API.
    // Zeroed first: the extra all-zero item marks the end of the menu.
    constexpr int RECORD = 0, FIRST_API = 1;
    Fl_Menu_Item items[FIRST_API + std::size(API_NAMES) + 1]{};

    // Made once and kept for the whole program; menus only borrow it.
    static Fl_SVG_Image* mic_icon = []{
        auto* icon = new Fl_SVG_Image(nullptr, ICON_MIC);
        icon->scale(16, 16);
        return icon;
    }();

    // Icon and text side by side. Only needs to live until popup() returns.
    Fl_Multi_Label record_label{};
    record_label.labela = reinterpret_cast<const char*>(mic_icon);
    record_label.typea = FL_IMAGE_LABEL;
    record_label.labelb = "  Record Input";
    record_label.typeb = FL_NORMAL_LABEL;
    items[RECORD].multi_label(&record_label);
    // Greyed out until speech-to-text is added to the project
    items[RECORD].flags = FL_MENU_INACTIVE | FL_MENU_DIVIDER;

    for (int i = 0; i < static_cast<int>(std::size(API_NAMES)); ++i){
        items[FIRST_API + i].label(API_NAMES[i]);
        items[FIRST_API + i].flags = FL_MENU_RADIO | (i == current ? FL_MENU_VALUE : 0);
    }

    // Opens just below the button. nullptr if the user clicks away.
    const Fl_Menu_Item* picked = items->popup(w->x(), w->y() + w->h(), nullptr, &items[FIRST_API + current]);
    if (picked == nullptr || picked == &items[RECORD]) return;

    const int index = static_cast<int>(picked - items) - FIRST_API;
    self->current_api = static_cast<Api>(index);
    w->tooltip(API_NAMES[index]);
}

// Search button and Enter in the input both end up here.
void SearchBar::on_submit(Fl_Widget*, void* data){
    static_cast<SearchBar*>(data)->do_callback();
}
