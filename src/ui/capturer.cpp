#include "ui/capturer.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <initializer_list>
#include <iostream>
#include <numbers>
#include <utility>
#include <vector>

// FLTK
#include <FL/Fl.H>
#include <FL/fl_ask.H>

#include "core/audio_buffer.h"
#include "core/clip_player.h"
#include "core/local_time.h"
#include "core/screenshot.h"
#include "core/settings.h"
#include "core/stash.h"
#include "ui/capture_window.h"
#include "ui/screen_window.h"
#include "ui/settings_store.h"
#include "ui/stash_store.h"
#include "ui/stash_transcriber.h"

// Seconds between checks for a new default output. Enumerating the
// devices takes a few milliseconds.
static constexpr double FOLLOW_TICK = 3;

// The chime's sample rate, and how long it plays at most, in seconds
static constexpr int CHIME_RATE = 48000;
static constexpr double CHIME_SECONDS = 0.4;

// Tones one after another, each a frequency (Hz) for seconds, faded in
// and out so they don't click. Quiet: it plays over the video, and ends up
// in the buffer too.
static std::vector<float> tones(std::initializer_list<std::pair<double, double>> notes){
    constexpr double VOLUME = 0.12, FADE = 0.008;
    std::vector<float> out;
    for (const auto& [frequency, seconds] : notes){
        const size_t n = static_cast<size_t>(seconds * CHIME_RATE);
        for (size_t i = 0; i < n; ++i){
            const double t = static_cast<double>(i) / CHIME_RATE;
            const double fade = std::min({1.0, t / FADE, (seconds - t) / FADE});
            out.push_back(static_cast<float>(VOLUME * fade * std::sin(2 * std::numbers::pi * frequency * t)));
        }
    }
    return out;
}

Capturer::Capturer(SettingsStore& settings, SharedTranscriber& transcriber, StashStore& stash)
    : settings(settings), transcriber(transcriber), stash(stash),
      stash_transcriber(std::make_unique<StashTranscriber>(settings, transcriber, stash)){
    apply_settings();
    settings_listener = settings.add_listener([this]{
        apply_settings();
        apply_shortcuts();
    });
    Fl::add_timeout(FOLLOW_TICK, on_follow_tick, this);

    try {
        hotkey = std::make_unique<GlobalHotkey>([this](HotkeyAction action){
            switch (action){
            case HotkeyAction::Capture: capture(); break;
            case HotkeyAction::ReadScreen: read_screen(); break;
            case HotkeyAction::Stash: stash_now(); break;
            }
        });
    } catch (const std::exception& e){
        // Shown on the Buffer and Screen Text settings pages too
        std::cerr << std::format("No capture shortcuts: {}\n", e.what());
        hotkey_errors.fill(e.what());
    }
    apply_shortcuts();
}

Capturer::~Capturer(){
    Fl::remove_timeout(on_follow_tick, this);
    Fl::remove_timeout(on_chime_done, this);
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

void Capturer::apply_shortcuts(){
    if (!hotkey || hotkey_paused) return;
    for (HotkeyAction action : HOTKEY_ACTIONS){
        // Every change to any setting comes here, and a shortcut that
        // failed is only tried again once it's changed
        const std::string& wanted = wanted_shortcut(action);
        std::string& applied = applied_shortcuts[index(action)];
        if (wanted == applied) continue;
        applied = wanted;

        std::string& error = hotkey_errors[index(action)];
        try {
            hotkey->set_shortcut(action, wanted);
            error.clear();
        } catch (const std::exception& e){
            // Shown on the settings page, next to the shortcut
            std::cerr << std::format("No shortcut {}: {}\n", wanted, e.what());
            error = e.what();
        }
    }
}

const std::string& Capturer::wanted_shortcut(HotkeyAction action) const {
    switch (action){
    case HotkeyAction::Capture: return settings.get().buffer.shortcut;
    case HotkeyAction::ReadScreen: return settings.get().screen_text.shortcut;
    case HotkeyAction::Stash: return settings.get().buffer.stash_shortcut;
    }
    return settings.get().buffer.shortcut;
}

void Capturer::pause_hotkey(bool paused){
    if (!hotkey || paused == hotkey_paused) return;
    hotkey_paused = paused;
    if (paused){
        // Taking them away can't fail
        for (HotkeyAction action : HOTKEY_ACTIONS){
            hotkey->set_shortcut(action, "");
            applied_shortcuts[index(action)].clear();
        }
    } else {
        apply_shortcuts();
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
            fl_alert("Nothing to capture: the buffer is off.\n\nTurn it on in Settings > Recording.");
        else
            fl_alert("Nothing to capture: the buffer isn't recording.\n\n%s", error_.c_str());
        return;
    }
    // The audio first, since the screenshot takes a tenth of a second.
    // Both before the window opens and covers anything.
    std::vector<float> audio = buffer->snapshot();
    Image screenshot;
    std::string screenshot_error;
    try {
        screenshot = take_screenshot();
    } catch (const std::exception& e){
        // Only needed for a card, which says so
        screenshot_error = e.what();
    }

    if (!capture_win) capture_win = std::make_unique<CaptureWindow>(settings, transcriber);
    capture_win->open(std::move(audio), AudioBuffer::SAMPLE_RATE, settings.get().buffer.selection_seconds,
                      std::move(screenshot), std::move(screenshot_error));
}

void Capturer::read_screen(){
    // Which game it is, for the part of the screen kept for it. Before the
    // screenshot, which may show the portal's dialog.
    std::string app = focused_app();
    Image screenshot;
    try {
        screenshot = take_screenshot();
    } catch (const std::exception& e){
        // Modal: waits for OK before returning
        fl_alert("Couldn't read the screen:\n\n%s", e.what());
        return;
    }

    if (!screen_win) screen_win = std::make_unique<ScreenWindow>(settings);
    screen_win->open(std::move(screenshot), std::move(app));
}

void Capturer::stash_now(){
    // No dialog: the user is watching something, maybe fullscreen
    if (!recording()){
        stash.set_last_error(settings.get().buffer.enabled ? std::format("The buffer isn't recording: {}", error_)
                                                           : "The buffer is off. Turn it on in Settings > Recording.");
        std::cerr << std::format("Couldn't stash: {}\n", stash.last_error());
        chime(false);
        return;
    }
    // Like capture(): the audio first. Which app it is before the
    // screenshot, which may show the portal's dialog.
    const std::vector<float> audio = buffer->snapshot();
    StashCapture capture{.audio = audio, .rate = AudioBuffer::SAMPLE_RATE,
                         .selection_seconds = settings.get().buffer.selection_seconds, .screenshot = nullptr,
                         .screenshot_error = "", .app = focused_app(), .time = unix_now()};
    Image screenshot;
    try {
        screenshot = take_screenshot();
        capture.screenshot = &screenshot;
    } catch (const std::exception& e){
        // Only needed for a card, which says so
        capture.screenshot_error = e.what();
    }

    StashEntry entry;
    try {
        entry = stash.add(capture);
    } catch (const std::exception& e){
        std::cerr << std::format("Couldn't stash: {}\n", e.what());
        chime(false);
        return;
    }
    chime(true);

    // The selection the capture window would start with
    if (settings.get().buffer.stash_transcribe){
        const size_t selected = std::min(audio.size(), static_cast<size_t>(entry.selection_seconds) * AudioBuffer::SAMPLE_RATE);
        stash_transcriber->transcribe(entry.id, std::vector<float>(audio.end() - static_cast<std::ptrdiff_t>(selected), audio.end()),
                                      AudioBuffer::SAMPLE_RATE);
    }
}

void Capturer::open_stashed(const StashEntry& entry){
    StashedLine line;
    try {
        line = load_stashed(stash.dir(), entry);
    } catch (const std::exception& e){
        // Modal: waits for OK before returning
        fl_alert("Couldn't open the stashed line:\n\n%s", e.what());
        return;
    }
    const std::string title = std::format("Stashed line, {}", format_time(local_time(entry.time), "%a %H:%M"));
    if (!capture_win) capture_win = std::make_unique<CaptureWindow>(settings, transcriber);
    // By the time a card is added, the line may be gone already, e.g.
    // deleted in the stash window meanwhile; removing it again does nothing
    capture_win->open(std::move(line.audio), line.rate, line.selection_seconds, std::move(line.screenshot),
                      std::move(line.screenshot_error), title, [this, id = entry.id]{
                          try {
                              stash.remove(id);
                          } catch (const std::exception& e){
                              std::cerr << std::format("Couldn't remove the stashed line {}: {}\n", id, e.what());
                          }
                      }, std::move(line.transcript));
}

void Capturer::chime(bool worked){
    if (!settings.get().buffer.stash_chime) return;
    // Two notes going up if it worked, one low one if not
    static const std::vector<float> ok = tones({{880, 0.07}, {1320, 0.11}});
    static const std::vector<float> failed = tones({{330, 0.22}});
    try {
        if (!chime_player) chime_player = std::make_unique<ClipPlayer>();
        chime_player->play(worked ? ok : failed, CHIME_RATE);
    } catch (const std::exception& e){
        std::cerr << std::format("Couldn't play the stash chime: {}\n", e.what());
        return;
    }
    Fl::remove_timeout(on_chime_done, this);
    Fl::add_timeout(CHIME_SECONDS, on_chime_done, this);
}

void Capturer::on_chime_done(void* data){
    Capturer* self = static_cast<Capturer*>(data);
    if (self->chime_player) self->chime_player->stop();
}
