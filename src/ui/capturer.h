#pragma once

#include <array>
#include <memory>
#include <string>

#include "ui/global_hotkey.h"

// Only pointers and references to these are stored here, so the full
// headers are included in capturer.cpp instead.
class AudioBuffer;
class CaptureWindow;
class ScreenWindow;
class SettingsStore;
class SharedTranscriber;

// Runs the rolling buffer of what the computer plays, from startup until
// the app quits, as the Buffer settings say: on or off, how long, and on
// Windows the capture shortcut. When the shortcut is pressed, it opens
// what's in the buffer in the capture window. The screen capture shortcut
// opens a screenshot in the screen window instead, to read its text.
//
// UI thread only, like the widgets.
class Capturer {
public:
    // settings and transcriber have to outlive it, which the capture
    // window transcribes with. Starts recording if the settings say to;
    // if that fails, error() says why. Starts listening for the
    // shortcuts; if that fails, hotkey_error() says why for each.
    Capturer(SettingsStore& settings, SharedTranscriber& transcriber);
    // Defined in capturer.cpp, where the classes in unique_ptrs are
    // complete. Stops recording and stops listening.
    ~Capturer();

    // The timeout and listener hold a pointer to it. There's one per app.
    Capturer(const Capturer&) = delete;
    Capturer& operator=(const Capturer&) = delete;

    // Whether the buffer is recording now
    bool recording() const;
    // Why it isn't recording though the settings say to, e.g. no
    // PulseAudio. Empty if it is, or it's off.
    const std::string& error() const { return error_; }
    // Why the shortcut for action can't be received. Empty if it can.
    const std::string& hotkey_error(HotkeyAction action) const { return hotkey_errors[index(action)]; }
    // Stops the shortcuts from capturing until they're unpaused, e.g.
    // while Settings waits for the keys of a new one: pressing the old one
    // there would capture instead of reaching it
    void pause_hotkey(bool paused);

    // Opens the last seconds in the capture window, with a screenshot
    // taken now, as the shortcut does
    void capture();
    // Opens a screenshot taken now in the screen window, which reads its
    // text, as the screen shortcut does
    void read_screen();

private:
    SettingsStore& settings;
    SharedTranscriber& transcriber;
    // From settings.add_listener
    int settings_listener = -1;
    // Created on the first start, so the audio system is only asked once
    // it's needed
    std::unique_ptr<AudioBuffer> buffer;
    std::string error_;
    // Null if it couldn't be set up
    std::unique_ptr<GlobalHotkey> hotkey;
    // Indexed by index(action)
    std::array<std::string, 2> hotkey_errors;
    // The shortcut last given to hotkey for each action, whether that
    // worked or not. Empty while there's none, e.g. paused.
    std::array<std::string, 2> applied_shortcuts;
    bool hotkey_paused = false;
    // Created on the first capture. Not plain members: constructing a
    // window makes it the current group, and the Capturer is created
    // before the main window.
    std::unique_ptr<CaptureWindow> capture_win;
    std::unique_ptr<ScreenWindow> screen_win;

    static size_t index(HotkeyAction action){ return action == HotkeyAction::Capture ? 0 : 1; }

    // Starts, stops or resizes the buffer to match the settings
    void apply_settings();
    // Gives hotkey the shortcuts the settings have, if they're others
    void apply_shortcuts();
    // Every few seconds: switches to the default output if it changed
    static void on_follow_tick(void* data);
};
