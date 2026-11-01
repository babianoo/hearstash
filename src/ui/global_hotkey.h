#pragma once

#include <functional>
#include <string>

// The shortcut that takes a capture, pressed in any app: mpv, a
// fullscreen browser, a game.
//
// On Linux the desktop owns global shortcuts (Wayland lets no app grab a
// key), so the user binds a key there to the command capture_command()
// gives, e.g. in hyprland.conf:
//
//   bind = SUPER, A, exec, /path/to/prototype --capture
//
// That command passes the press on to the running app through a socket in
// $XDG_RUNTIME_DIR, which only the user can open.
//
// On Windows, RegisterHotKey isn't done yet.
class GlobalHotkey {
public:
    // on_press is called on the UI thread for each press. Throws
    // std::runtime_error if presses can't be received, e.g. when another
    // copy of the app is already running and receiving them.
    explicit GlobalHotkey(std::function<void()> on_press);
    // Stops receiving presses
    ~GlobalHotkey();

    // FLTK keeps a pointer to it while it listens
    GlobalHotkey(const GlobalHotkey&) = delete;
    GlobalHotkey& operator=(const GlobalHotkey&) = delete;

private:
    std::function<void()> on_press;
    // The socket presses arrive on; -1 if none
    int listener = -1;

    // Static because FLTK's fd callbacks are plain functions; the
    // GlobalHotkey comes in through data
    static void on_connection(int fd, void* data);
    static void on_message(int fd, void* data);
};

// The argument that makes the app send a press and quit, instead of
// starting
inline constexpr const char* CAPTURE_ARG = "--capture";

// Sends a press to the running app, for main() when it's started with
// CAPTURE_ARG. Throws std::runtime_error if no copy of the app is running.
void send_hotkey_press();

// The command to bind to a key, with this executable's full path, quoted
// if it has spaces. Empty where there's no such command (Windows).
std::string capture_command();
