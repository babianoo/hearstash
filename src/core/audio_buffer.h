#pragma once

#include <memory>
#include <vector>

// Always keeps the last seconds of what the computer plays (not the
// microphone), so a line that was just heard can be taken back out:
// mono, 48 kHz, floats from -1 to 1. Time with nothing playing is kept as
// silence, so the last 30 seconds are always the last 30 seconds of time.
//
// On Linux it records the default output's monitor, through PulseAudio
// or PipeWire's PulseAudio server. On Windows, WASAPI's loopback isn't
// done yet, and start() throws.
//
// Everything is called from one thread, the UI thread in the app. The
// audio arrives on a thread of miniaudio's, which only writes into memory
// set aside up front, and never locks.
class AudioBuffer {
public:
    // Better for the clips on cards than whisper's 16 kHz, which sound
    // muffled. Resampled for whisper.
    static constexpr int SAMPLE_RATE = 48000;
    // Kept on top of the seconds asked for, so snapshot() can copy them
    // while the audio thread keeps writing into this
    static constexpr int MARGIN_SECONDS = 2;

    enum class Backend {
        // PulseAudio on Linux
        System,
        // A pretend output that plays silence, for tests
        Null,
    };

    // Holds seconds of audio, and doesn't record until start(). Throws
    // std::runtime_error if there's no audio system to use.
    explicit AudioBuffer(int seconds, Backend backend = Backend::System);
    // Stops recording
    ~AudioBuffer();

    // miniaudio's device keeps a pointer to it
    AudioBuffer(const AudioBuffer&) = delete;
    AudioBuffer& operator=(const AudioBuffer&) = delete;

    // Starts recording what the default output plays. Does nothing if it's
    // already recording. Throws std::runtime_error if it can't, e.g. on a
    // system without PulseAudio.
    void start();
    // Stops recording, and forgets what was recorded: it's never kept
    // longer than it has to be
    void stop();
    bool running() const;

    int seconds() const { return length; }
    // Holds seconds of audio from now on. What was recorded is forgotten,
    // and recording goes on if it was. Throws like start().
    void set_seconds(int seconds);

    // The last seconds() of audio, oldest first, or less if it hasn't been
    // recording that long. Takes milliseconds.
    std::vector<float> snapshot() const;

    // Restarts recording if the default output changed since it started,
    // e.g. headphones were plugged in, so it records the one that's
    // playing. What was recorded is kept. Call every few seconds. Throws
    // like start().
    void follow_default_output();

private:
    int length;
    // Holds the miniaudio objects and the ring, so what includes this
    // doesn't need miniaudio's 4 MB header
    struct Impl;
    std::unique_ptr<Impl> impl;

    void open_device();
    void close_device();
};
