#include "ui/capturer.h"

#include <exception>
#include <format>
#include <iostream>

// FLTK
#include <FL/Fl.H>
#include <FL/fl_ask.H>

#include "core/audio_buffer.h"
#include "core/settings.h"
#include "ui/capture_window.h"
#include "ui/global_hotkey.h"
#include "ui/settings_store.h"

// Seconds between checks for a new default output. Enumerating the
// devices takes a few milliseconds.
static constexpr double FOLLOW_TICK = 3;

Capturer::Capturer(SettingsStore& settings, SharedTranscriber& transcriber)
    : settings(settings), transcriber(transcriber){
    apply_settings();
    settings_listener = settings.add_listener([this]{ apply_settings(); });
    Fl::add_timeout(FOLLOW_TICK, on_follow_tick, this);

    try {
        hotkey = std::make_unique<GlobalHotkey>([this]{ capture(); });
    } catch (const std::exception& e){
        // Shown on the Buffer settings page too
        std::cerr << std::format("No capture shortcut: {}\n", e.what());
        hotkey_error_ = e.what();
    }
}

Capturer::~Capturer(){
    Fl::remove_timeout(on_follow_tick, this);
    settings.remove_listener(settings_listener);
}

bool Capturer::recording() const {
    return buffer && buffer->running();
}

void Capturer::apply_settings(){
    const BufferSettings& wanted = settings.get().buffer;
    if (!wanted.enabled){
        // Forgets what was recorded too
        if (buffer) buffer->stop();
        error_.clear();
        return;
    }
    // Every change to any setting comes here. Only a change to the buffer's
    // own, or a failure to retry, is worth touching the device for.
    if (recording() && buffer->seconds() == wanted.seconds) return;

    try {
        if (!buffer) buffer = std::make_unique<AudioBuffer>(wanted.seconds);
        buffer->set_seconds(wanted.seconds);
        buffer->start();
        error_.clear();
    } catch (const std::exception& e){
        // Not in a dialog: it would pop up at every start, and on every
        // setting changed. The Buffer settings page shows it.
        if (error_ != e.what()) std::cerr << std::format("Not recording what the computer plays: {}\n", e.what());
        error_ = e.what();
    }
}

void Capturer::on_follow_tick(void* data){
    Capturer* self = static_cast<Capturer*>(data);
    if (self->recording()){
        try {
            self->buffer->follow_default_output();
        } catch (const std::exception& e){
            // E.g. the new output went away again. The next tick retries
            // through apply_settings(), since it's no longer recording.
            self->error_ = e.what();
        }
    } else if (self->settings.get().buffer.enabled){
        // E.g. the audio server wasn't up yet when the app started
        self->apply_settings();
    }
    Fl::repeat_timeout(FOLLOW_TICK, on_follow_tick, data);
}

void Capturer::capture(){
    if (!recording()){
        // Modal: waits for OK before returning
        if (!settings.get().buffer.enabled)
            fl_alert("Nothing to capture: the buffer is off.\n\nTurn it on in Settings > Buffer.");
        else
            fl_alert("Nothing to capture: the buffer isn't recording.\n\n%s", error_.c_str());
        return;
    }
    if (!capture_win) capture_win = std::make_unique<CaptureWindow>(settings, transcriber);
    capture_win->open(buffer->snapshot(), AudioBuffer::SAMPLE_RATE, settings.get().buffer.selection_seconds);
}
