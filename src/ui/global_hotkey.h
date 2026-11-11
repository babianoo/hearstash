#pragma once

#include <cstddef>
#include <functional>
#include <string>

// What a global shortcut does: a capture of what the computer played, one
// of the text on the screen, or a capture saved for later without opening
enum class HotkeyAction { Capture, ReadScreen, Stash };

// How many there are, e.g. to size an array indexed by them
inline constexpr size_t HOTKEY_ACTION_COUNT = 3;

// Every action, in order
inline constexpr HotkeyAction HOTKEY_ACTIONS[HOTKEY_ACTION_COUNT] = {HotkeyAction::Capture, HotkeyAction::ReadScreen,
                                                                     HotkeyAction::Stash};

// The shortcuts that take a capture, pressed in any app: mpv, a
// fullscreen browser, a game. One for each HotkeyAction.
//
// On Linux the desktop owns global shortcuts (Wayland lets no app grab a
// key), so the user binds a key there to the command hotkey_command()
// gives for each, e.g. in hyprland.conf:
//
//   bind = SUPER, A, exec, /path/to/hearstash --capture
//   bind = SUPER, S, exec, /path/to/hearstash --read-screen
//   bind = SUPER, Z, exec, /path/to/hearstash --stash
//
// That command passes the press on to the running app through a socket in
// $XDG_RUNTIME_DIR, which only the user can open.
//
// On Windows the app registers the keys itself, with RegisterHotKey, on a
// hidden window of its own. The keys are settings (set_shortcut). The
// commands work there too, passing the press on to that window.
class GlobalHotkey {
public:
    // on_press is called on the UI thread for each press, with what it's
    // for. Throws std::runtime_error if presses can't be received, e.g.
    // when another copy of the app is already running and receiving them.
    explicit GlobalHotkey(std::function<void(HotkeyAction)> on_press);
    // Stops receiving presses
    ~GlobalHotkey();

    // FLTK, or on Windows the hidden window, keeps a pointer to it
    GlobalHotkey(const GlobalHotkey&) = delete;
    GlobalHotkey& operator=(const GlobalHotkey&) = delete;

    // On Windows: makes shortcut, as parse_shortcut reads it, the key that
    // presses for action, in place of the one before. Empty for none.
    // Throws std::runtime_error if it can't, e.g. another app has it
    // already; then there's none. Does nothing on Linux, where the desktop
    // owns the keys.
    void set_shortcut(HotkeyAction action, const std::string& shortcut);

private:
    std::function<void(HotkeyAction)> on_press;
#ifdef _WIN32
    // The hidden window presses arrive at, an HWND, so this header doesn't
    // need windows.h
    void* window = nullptr;
    // Whether a shortcut is registered on it for each action
    bool registered[HOTKEY_ACTION_COUNT] = {};
#else
    // The socket presses arrive on; -1 if none
    int listener = -1;

    // Static because FLTK's fd callbacks are plain functions; the
    // GlobalHotkey comes in through data
    static void on_connection(int fd, void* data);
    static void on_message(int fd, void* data);
#endif
};

// The arguments that make the app send a press and quit, instead of
// starting
inline constexpr const char* CAPTURE_ARG = "--capture";
inline constexpr const char* READ_SCREEN_ARG = "--read-screen";
inline constexpr const char* STASH_ARG = "--stash";

// The argument that sends a press for action
const char* hotkey_arg(HotkeyAction action);

// Sends a press for action to the running app, for main() when it's
// started with one of the arguments above. Throws std::runtime_error
// if no copy of the app is running.
void send_hotkey_press(HotkeyAction action);

// The command to bind to a key for action, with this executable's full
// path, quoted if it has spaces. Empty on Windows, where the keys are set
// in the app.
std::string hotkey_command(HotkeyAction action);
