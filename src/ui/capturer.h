#pragma once

#include <array>
#include <memory>
#include <string>

#include "ui/global_hotkey.h"

// Only pointers and references to these are stored here, so the full
// headers are included in capturer.cpp instead.
class AudioBuffer;
class CaptureWindow;
class ClipPlayer;
class ScreenWindow;
class SettingsStore;
class SharedTranscriber;
class StashStore;
class StashTranscriber;
struct StashEntry;

// Runs the rolling buffer of what the computer plays, from startup until
// the app quits, as the Buffer settings say: on or off, how long, and on
// Windows the capture shortcut. When the shortcut is pressed, it opens
// what's in the buffer in the capture window. The screen capture shortcut
// opens a screenshot in the screen window instead, to read its text. The
// stash shortcut saves what the capture shortcut would open, without
// opening anything, to look at after the episode.
//
// UI thread only, like the widgets.
class Capturer {
public:
    // settings, transcriber and stash have to outlive it: the capture
    // window transcribes with transcriber, and stash is where stashed
    // lines go. Starts recording if the settings say to;
    // if that fails, error() says why. Starts listening for the
    // shortcuts; if that fails, hotkey_error() says why for each.
    Capturer(SettingsStore& settings, SharedTranscriber& transcriber, StashStore& stash);
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
    // Saves the last seconds and a screenshot taken now in the stash,
    // without opening a window or taking the focus, as the stash shortcut
    // does. A chime says whether it worked, if the settings say to; why it
    // didn't is the stash's last_error(). If the settings say to, its
    // selection is then transcribed in the background.
    void stash_now();
    // Opens a stashed line in the capture window, as a capture would have
    // been, with its transcript if it has one. Adding a card from it
    // removes it from the stash. Says in a dialog if it can't be read.
    void open_stashed(const StashEntry& entry);

private:
    SettingsStore& settings;
    SharedTranscriber& transcriber;
    StashStore& stash;
    // From settings.add_listener
    int settings_listener = -1;
    // Created on the first start, so the audio system is only asked once
    // it's needed
    std::unique_ptr<AudioBuffer> buffer;
    std::string error_;
    // Null if it couldn't be set up
    std::unique_ptr<GlobalHotkey> hotkey;
    // Indexed by index(action)
    std::array<std::string, HOTKEY_ACTION_COUNT> hotkey_errors;
    // The shortcut last given to hotkey for each action, whether that
    // worked or not. Empty while there's none, e.g. paused.
    std::array<std::string, HOTKEY_ACTION_COUNT> applied_shortcuts;
    bool hotkey_paused = false;
    // Created on the first capture. Not plain members: constructing a
    // window makes it the current group, and the Capturer is created
    // before the main window.
    std::unique_ptr<CaptureWindow> capture_win;
    std::unique_ptr<ScreenWindow> screen_win;
    // Plays the stash chime. Created on the first one, so a machine
    // without audio output only finds out then.
    std::unique_ptr<ClipPlayer> chime_player;
    // Transcribes stashed lines if the settings say to
    std::unique_ptr<StashTranscriber> stash_transcriber;

    static size_t index(HotkeyAction action){ return static_cast<size_t>(action); }
    // The shortcut the settings have for action, on Windows
    const std::string& wanted_shortcut(HotkeyAction action) const;
    // A rising chime if it worked, a low one if not, if the settings say to
    void chime(bool worked);
    // Once the chime has played: closes the output, which ClipPlayer wants
    static void on_chime_done(void* data);

    // Starts, stops or resizes the buffer to match the settings
    void apply_settings();
    // Gives hotkey the shortcuts the settings have, if they're others
    void apply_shortcuts();
    // Every few seconds: switches to the default output if it changed
    static void on_follow_tick(void* data);
};
