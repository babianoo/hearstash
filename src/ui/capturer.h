#pragma once

#include <memory>
#include <string>

// Only pointers and references to these are stored here, so the full
// headers are included in capturer.cpp instead.
class AudioBuffer;
class CaptureWindow;
class GlobalHotkey;
class SettingsStore;
class SharedTranscriber;

// Runs the rolling buffer of what the computer plays, from startup until
// the app quits, as the Buffer settings say: on or off, how long, and on
// Windows the capture shortcut. When the shortcut is pressed, it opens
// what's in the buffer in the capture window.
//
// UI thread only, like the widgets.
class Capturer {
public:
    // settings and transcriber have to outlive it, which the capture
    // window transcribes with. Starts recording if the settings say to;
    // if that fails, error() says why. Starts listening for the shortcut;
    // if that fails, hotkey_error() says why.
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
    // Why the capture shortcut can't be received. Empty if it can.
    const std::string& hotkey_error() const { return hotkey_error_; }
    // Stops the shortcut from capturing until it's unpaused, e.g. while
    // Settings waits for the keys of a new one: pressing the old one there
    // would capture instead of reaching it
    void pause_hotkey(bool paused);

    // Opens the last seconds in the capture window, with a screenshot
    // taken now, as the shortcut does
    void capture();

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
    std::string hotkey_error_;
    // The shortcut last given to hotkey, whether that worked or not. Empty
    // while there's none, e.g. paused.
    std::string applied_shortcut;
    bool hotkey_paused = false;
    // Created on the first capture. Not a plain member: constructing a
    // window makes it the current group, and the Capturer is created
    // before the main window.
    std::unique_ptr<CaptureWindow> capture_win;

    // Starts, stops or resizes the buffer to match the settings
    void apply_settings();
    // Gives hotkey the shortcut the settings have, if it's another
    void apply_shortcut();
    // Every few seconds: switches to the default output if it changed
    static void on_follow_tick(void* data);
};
