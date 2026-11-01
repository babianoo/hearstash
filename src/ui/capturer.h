#pragma once

#include <memory>
#include <string>

// Only pointers and references to these are stored here, so the full
// headers are included in capturer.cpp instead.
class AudioBuffer;
class SettingsStore;

// Runs the rolling buffer of what the computer plays, from startup until
// the app quits, as the Buffer settings say: on or off, and how long.
//
// UI thread only, like the widgets.
class Capturer {
public:
    // settings has to outlive it. Starts recording if the settings say to;
    // if that fails, error() says why.
    explicit Capturer(SettingsStore& settings);
    // Stops recording and stops listening to settings
    ~Capturer();

    // The timeout and listener hold a pointer to it. There's one per app.
    Capturer(const Capturer&) = delete;
    Capturer& operator=(const Capturer&) = delete;

    // Whether the buffer is recording now
    bool recording() const;
    // Why it isn't recording though the settings say to, e.g. no
    // PulseAudio. Empty if it is, or it's off.
    const std::string& error() const { return error_; }

private:
    SettingsStore& settings;
    // From settings.add_listener
    int settings_listener = -1;
    // Created on the first start, so the audio system is only asked once
    // it's needed
    std::unique_ptr<AudioBuffer> buffer;
    std::string error_;

    // Starts, stops or resizes the buffer to match the settings
    void apply_settings();
    // Every few seconds: switches to the default output if it changed
    static void on_follow_tick(void* data);
};
