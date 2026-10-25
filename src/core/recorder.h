#pragma once

#include <memory>
#include <string>
#include <vector>

// Records from a microphone into memory, in the format Transcriber wants:
// mono, 16 kHz, floats from -1 to 1. Whatever the microphone gives is
// converted to that. A recording holds at most MAX_SECONDS: after that
// nothing more is kept, and full() says to stop.
//
// Everything is called from one thread, the UI thread in the app. The
// audio arrives on a thread of miniaudio's, which only writes into memory
// set aside before recording starts.
class Recorder {
public:
    static constexpr int SAMPLE_RATE = 16000;
    // About what whisper handles in one pass
    static constexpr int MAX_SECONDS = 30;

    enum class Backend {
        // WASAPI on Windows, PulseAudio or ALSA on Linux
        System,
        // A pretend microphone that records silence, for tests
        Null,
    };

    // Throws std::runtime_error if there's no audio system to use
    explicit Recorder(Backend backend = Backend::System);
    // Stops a recording still running
    ~Recorder();

    // miniaudio's device keeps a pointer to it
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // The microphones' names, as start() takes them. Asked anew each time,
    // so one plugged in since shows up.
    std::vector<std::string> input_devices();

    // Starts recording from the microphone named device, or the default
    // one if device is empty or isn't plugged in any more. Does nothing if
    // it's already recording. Throws std::runtime_error if the microphone
    // can't be opened, e.g. when there's none or Windows blocks it.
    void start(const std::string& device = "");
    // Stops and returns what was recorded; empty if it wasn't recording
    std::vector<float> stop();

    bool recording() const;
    // MAX_SECONDS are recorded, so stop() should be called: miniaudio's
    // thread can't stop the device itself
    bool full() const;
    // Of the recording so far
    double seconds() const;
    // How loud the latest audio was, from 0 to 1, for a level meter
    float level() const;

private:
    // Holds the miniaudio objects, so what includes this doesn't need its
    // 4 MB header
    struct Impl;
    std::unique_ptr<Impl> impl;
};
