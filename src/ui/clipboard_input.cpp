#include "ui/clipboard_input.h"

#include <algorithm>
#include <cctype>
#include <string_view>

// FLTK
#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Menu_Item.H>

#include "ui/clipboard.h"

int ClipboardInput::handle(int event){
    if (event == FL_KEYBOARD){
        // Same combinations as Fl_Input
        const int mods = Fl::event_state() & (FL_META | FL_CTRL | FL_ALT);
        const bool shift = Fl::event_state(FL_SHIFT);
        const int key = Fl::event_key();
        // Still taken when secret, so Fl_Input doesn't copy either
        if ((key == 'c' && mods == FL_COMMAND) || (key == FL_Insert && mods == FL_CTRL)){
            if (!is_secret()) copy_selection();
            return 1;
        }
        if (key == 'x' && mods == FL_COMMAND){
            if (!is_secret()){
                copy_selection();
                cut();
            }
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

void ClipboardInput::copy_selection(){
    const int from = std::min(insert_position(), mark()), to = std::max(insert_position(), mark());
    if (from != to) clipboard_copy(std::string_view(value() + from, to - from));
}

void ClipboardInput::show_menu(){
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
    if (is_secret()){
        menu[0].deactivate();
        menu[1].deactivate();
    }
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
