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
//   bind = SUPER, A, exec, /path/to/hearstash --capture
//
// That command passes the press on to the running app through a socket in
// $XDG_RUNTIME_DIR, which only the user can open.
//
// On Windows the app registers the key itself, with RegisterHotKey, on a
// hidden window of its own. The key is a setting (set_shortcut). The
// command works there too, passing the press on to that window.
class GlobalHotkey {
public:
    // on_press is called on the UI thread for each press. Throws
    // std::runtime_error if presses can't be received, e.g. when another
    // copy of the app is already running and receiving them.
    explicit GlobalHotkey(std::function<void()> on_press);
    // Stops receiving presses
    ~GlobalHotkey();

    // FLTK, or on Windows the hidden window, keeps a pointer to it
    GlobalHotkey(const GlobalHotkey&) = delete;
    GlobalHotkey& operator=(const GlobalHotkey&) = delete;

    // On Windows: makes shortcut, as parse_shortcut reads it, the key that
    // presses, in place of the one before. Empty for none. Throws
    // std::runtime_error if it can't, e.g. another app has it already; then
    // there's none. Does nothing on Linux, where the desktop owns the key.
    void set_shortcut(const std::string& shortcut);

private:
    std::function<void()> on_press;
#ifdef _WIN32
    // The hidden window presses arrive at, an HWND, so this header doesn't
    // need windows.h
    void* window = nullptr;
    // Whether a shortcut is registered on it
    bool registered = false;
#else
    // The socket presses arrive on; -1 if none
    int listener = -1;

    // Static because FLTK's fd callbacks are plain functions; the
    // GlobalHotkey comes in through data
    static void on_connection(int fd, void* data);
    static void on_message(int fd, void* data);
#endif
};

// The argument that makes the app send a press and quit, instead of
// starting
inline constexpr const char* CAPTURE_ARG = "--capture";

// Sends a press to the running app, for main() when it's started with
// CAPTURE_ARG. Throws std::runtime_error if no copy of the app is running.
void send_hotkey_press();

// The command to bind to a key, with this executable's full path, quoted
// if it has spaces. Empty on Windows, where the key is set in the app.
std::string capture_command();
