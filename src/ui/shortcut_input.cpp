#include "ui/shortcut_input.h"

#include <format>

// FLTK
#include <FL/Fl.H>

#include "core/shortcut.h"
#include "ui/theme.h"

static constexpr const char* PROMPT = "Press the keys, or Esc";

// FLTK's key as Shortcut::key names it. Empty for a key that can't be in
// a shortcut.
static std::string key_name(int key){
    // FLTK gives letters as lower case, whatever Shift does
    if (key >= 'a' && key <= 'z') return std::string(1, static_cast<char>(key - 'a' + 'A'));
    if (key >= '0' && key <= '9') return std::string(1, static_cast<char>(key));
    if (key > FL_F && key <= FL_F + 24) return std::format("F{}", key - FL_F);
    switch (key){
        case ' ': return "Space";
        case FL_Insert: return "Insert";
        case FL_Delete: return "Delete";
        case FL_Home: return "Home";
        case FL_End: return "End";
        case FL_Page_Up: return "PageUp";
        case FL_Page_Down: return "PageDown";
        case FL_Up: return "Up";
        case FL_Down: return "Down";
        case FL_Left: return "Left";
        case FL_Right: return "Right";
        case FL_Pause: return "Pause";
        default: return "";
    }
}

// The modifiers held now, with the key
static Shortcut held(const std::string& key){
    return Shortcut{
        // FLTK calls the Windows key Meta
        .win = Fl::event_state(FL_META) != 0,
        .ctrl = Fl::event_state(FL_CTRL) != 0,
        .alt = Fl::event_state(FL_ALT) != 0,
        .shift = Fl::event_state(FL_SHIFT) != 0,
        .key = key,
    };
}

ShortcutInput::ShortcutInput(int x, int y, int w, int h) : Fl_Input(x, y, w, h){
    // Typed into only through handle()
    readonly(1);
}

void ShortcutInput::shortcut(const std::string& shortcut){
    current = shortcut;
    if (!waiting) show(current, false);
}

void ShortcutInput::show(const std::string& text, bool dim){
    value(text.c_str());
    textcolor(dim ? DIM : FL_FOREGROUND_COLOR);
    redraw();
}

void ShortcutInput::set_waiting(bool now_waiting){
    if (now_waiting == waiting) return;
    waiting = now_waiting;
    if (waiting) show(PROMPT, true);
    else show(current, false);
    if (on_waiting) on_waiting(waiting);
}

int ShortcutInput::handle(int event){
    switch (event){
    case FL_PUSH:
        // A click starts waiting, also after Escape gave up
        if (Fl::focus() != this) take_focus();
        set_waiting(true);
        return 1;
    case FL_FOCUS:
        Fl_Input::handle(event);
        set_waiting(true);
        return 1;
    case FL_UNFOCUS:
        set_waiting(false);
        return Fl_Input::handle(event);
    case FL_KEYBOARD: {
        const int key = Fl::event_key();
        // To the next widget, and Escape once more closes the window
        if (!waiting || (key == FL_Tab && !Fl::event_state(FL_CTRL | FL_ALT | FL_META))) return 0;
        if (key == FL_Escape){
            set_waiting(false);
            return 1;
        }
        // Ctrl, Alt, Shift or Win by itself: show them, and wait for the key
        if (key >= FL_Shift_L && key <= FL_Alt_R){
            const std::string held_now = format_shortcut(held(""));
            show(held_now.empty() ? PROMPT : held_now, true);
            return 1;
        }

        const std::string name = key_name(key);
        if (name.empty()){
            show("That key can't be in a shortcut", true);
            return 1;
        }
        const Shortcut pressed = held(name);
        if (const std::string problem = shortcut_problem(pressed); !problem.empty()){
            show(std::format("{}: {}", format_shortcut(pressed), problem), true);
            return 1;
        }
        current = format_shortcut(pressed);
        set_waiting(false);
        do_callback();
        return 1;
    }
    default:
        return Fl_Input::handle(event);
    }
}
