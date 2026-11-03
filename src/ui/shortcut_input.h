#pragma once

#include <functional>
#include <string>

// FLTK
#include <FL/Fl_Input.H>

// A box that shows a key combination, and takes a new one from the keys
// pressed while it has the focus, e.g. Win+Alt+C: click it, then press
// them. Escape gives up and keeps the one before. A combination
// shortcut_problem() rejects isn't taken, and the box says why.
//
// The callback runs once a new one is taken; shortcut() has it then.
class ShortcutInput : public Fl_Input {
public:
    ShortcutInput(int x, int y, int w, int h);

    int handle(int event) override;

    // As format_shortcut writes it
    const std::string& shortcut() const { return current; }
    void shortcut(const std::string& shortcut);
    // Gives up waiting for keys, as Escape does. For when it's hidden: it
    // keeps the focus then, so it doesn't hear about it.
    void stop_waiting(){ set_waiting(false); }

    // Called with true when it starts waiting for keys, and false when it
    // stops, e.g. so the app's own shortcut doesn't capture meanwhile
    std::function<void(bool waiting)> on_waiting;

private:
    std::string current;
    bool waiting = false;

    void set_waiting(bool waiting);
    // Shows text, dimmed or not
    void show(const std::string& text, bool dim);
};
