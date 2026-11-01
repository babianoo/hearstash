#pragma once

#include <memory>
#include <span>

// Plays a clip of mono audio on the default output, e.g. the selection in
// a capture, so the user can check it's the right line.
//
// Everything is called from one thread, the UI thread in the app. The
// audio is played by a thread of miniaudio's, which only reads from a copy
// made before playing starts.
class ClipPlayer {
public:
    enum class Backend {
        // WASAPI on Windows, PulseAudio or ALSA on Linux
        System,
        // A pretend output that plays nothing, for tests
        Null,
    };

    // Throws std::runtime_error if there's no audio system to use
    explicit ClipPlayer(Backend backend = Backend::System);
    // Stops playing
    ~ClipPlayer();

    // miniaudio's device keeps a pointer to it
    ClipPlayer(const ClipPlayer&) = delete;
    ClipPlayer& operator=(const ClipPlayer&) = delete;

    // Plays samples, rate a second, from the start, stopping whatever was
    // playing first. Copies them. Throws std::runtime_error if the output
    // can't be opened.
    void play(std::span<const float> samples, int rate);
    // Stops playing. Does nothing if it isn't.
    void stop();

    // Whether it's still playing. Becomes false once the last sample has
    // been played, but the device is only closed by stop(), which should be
    // called then: miniaudio's thread can't close it itself.
    bool playing() const;
    // How many samples have been played so far
    size_t position() const;

private:
    // Holds the miniaudio objects, so what includes this doesn't need its
    // 4 MB header
    struct Impl;
    std::unique_ptr<Impl> impl;
};
