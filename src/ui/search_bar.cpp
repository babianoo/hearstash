#include "ui/search_bar.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string_view>

// FLTK
#include <FL/Fl_Box.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Multi_Label.H>
#include <FL/Fl_SVG_Image.H>

#include "ui/clipboard.h"
#include "ui/icons.h"
#include "ui/theme.h"

// Indexed by Api
static constexpr const char* API_NAMES[] = {"Jisho", "DeepL", "MyMemory"};

// Fl_Input whose copy, cut and paste go through ui/clipboard.h instead of
// FLTK's own clipboard, which loses what's copied on Wayland. Same keys
// and right-click menu as Fl_Input, whose versions can't be overridden.
class ClipboardInput : public Fl_Input {
public:
    using Fl_Input::Fl_Input;

    int handle(int event) override {
        if (event == FL_KEYBOARD){
            // Same combinations as Fl_Input
            const int mods = Fl::event_state() & (FL_META | FL_CTRL | FL_ALT);
            const bool shift = Fl::event_state(FL_SHIFT);
            const int key = Fl::event_key();
            if ((key == 'c' && mods == FL_COMMAND) || (key == FL_Insert && mods == FL_CTRL)){
                copy_selection();
                return 1;
            }
            if (key == 'x' && mods == FL_COMMAND){
                copy_selection();
                cut();
                return 1;
            }
            if ((key == 'v' && mods == FL_COMMAND) || (key == FL_Insert && mods == 0 && shift)){
                clipboard_paste(*this);
                return 1;
            }
        }

        if (event == FL_PUSH && Fl::event_button() == FL_RIGHT_MOUSE){
            if (Fl::focus() != this){
                Fl::focus(this);
                handle(FL_FOCUS);
            }
            show_menu();
            return 1;
        }

        return Fl_Input::handle(event);
    }

private:
    void copy_selection(){
        const int from = std::min(insert_position(), mark()), to = std::max(insert_position(), mark());
        if (from != to) clipboard_copy(std::string_view(value() + from, to - from));
    }

    // Like Fl_Input's: a click outside the selection first selects the
    // word (or run of spaces) under the mouse, or moves the cursor to the
    // end when it's past the text
    void show_menu(){
        const int old_pos = insert_position(), old_mark = mark();
        const Fl_Boxtype b = box();
        Fl_Input_::handle_mouse(x() + Fl::box_dx(b), y() + Fl::box_dy(b), w() - Fl::box_dw(b), h() - Fl::box_dh(b), 0);
        const int pos = insert_position();

        auto is_space = [](unsigned c){ return c < 128 && std::isspace(static_cast<int>(c)); };
        if ((old_pos < pos && pos < old_mark) || (old_mark < pos && pos < old_pos)){
            insert_position(old_pos, old_mark);    // Inside the selection: keep it
        } else if (index(pos) == 0){
            insert_position(pos, pos);
        } else if (is_space(index(pos))){
            int from = pos, to = pos + 1;
            while (from > 0 && is_space(index(from - 1))) --from;
            while (to < size() && is_space(index(to))) ++to;
            insert_position(from, to);
        } else {
            insert_position(word_start(pos), word_end(pos));
        }

        // The labels are read here, since an app may change them. The last
        // item stays empty: it ends the menu.
        Fl_Menu_Item menu[4]{};
        menu[0].label(Fl_Input::cut_menu_text);
        menu[1].label(Fl_Input::copy_menu_text);
        menu[2].label(Fl_Input::paste_menu_text);
        fl_cursor(FL_CURSOR_DEFAULT);
        const Fl_Menu_Item* chosen = menu->popup(Fl::event_x(), Fl::event_y());
        if (chosen == &menu[0]){
            copy_selection();
            cut();
        } else if (chosen == &menu[1]){
            copy_selection();
        } else if (chosen == &menu[2]){
            clipboard_paste(*this);
        }
    }
};

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
