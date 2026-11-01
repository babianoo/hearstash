#include "ui/capture_window.h"

#include <algorithm>
#include <exception>
#include <format>

// FLTK
#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>

#include "core/clip_player.h"
#include "ui/theme.h"
#include "ui/waveform.h"

// Space around and between things
static constexpr int MARGIN = 10;
static constexpr int WAVEFORM_H = 160;
static constexpr int BTN_W = 90, BTN_H = 28;
// Seconds between playhead moves while playing
static constexpr double PLAY_TICK = 1.0 / 30;

CaptureWindow::CaptureWindow(){
    // win is the current group here, like in MainWindow
    const int inner_w = win.w() - 2 * MARGIN;
    waveform = new Waveform(MARGIN, MARGIN, inner_w, WAVEFORM_H);
    waveform->on_selection([this]{
        // What plays is the selection as it was
        stop_playing();
        refresh_selection();
    });

    const int row_y = MARGIN + WAVEFORM_H + MARGIN;
    play_btn = new Fl_Button(MARGIN, row_y, BTN_W, BTN_H, "Play");
    play_btn->box(FL_ROUND_UP_BOX);
    play_btn->clear_visible_focus();
    play_btn->shortcut(' ');
    play_btn->tooltip("Play the selection (Space)");
    play_btn->callback(on_play, this);

    selection_info = new Fl_Box(MARGIN + BTN_W + MARGIN, row_y, inner_w - BTN_W - MARGIN, BTN_H);
    selection_info->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    selection_info->labelcolor(DIM);

    // No resizable() on purpose: FLTK then keeps the window at a fixed
    // size, which also makes tiling window managers float it
    win.end();
    win.callback(on_close, this);
}

CaptureWindow::~CaptureWindow(){
    Fl::remove_timeout(on_play_tick, this);
}

void CaptureWindow::open(std::vector<float> new_audio, int new_rate, int selection_seconds){
    stop_playing();
    audio = std::move(new_audio);
    rate = new_rate;
    waveform->audio(audio, rate);
    const size_t selected = std::min(audio.size(), static_cast<size_t>(selection_seconds) * static_cast<size_t>(rate));
    waveform->selection(audio.size() - selected, audio.size());
    refresh_selection();

    // Wayland doesn't let an app raise a window that's already shown, but
    // a window that's shown anew comes up in front, with the focus
    if (win.shown()) win.hide();
    win.show();
}

void CaptureWindow::refresh_selection(){
    const auto [begin, end] = waveform->selection();
    if (begin == end){
        selection_info->copy_label("Drag on the waveform to select a line.");
        play_btn->deactivate();
    } else {
        selection_info->copy_label(std::format("{:.1f} s selected", static_cast<double>(end - begin) / rate).c_str());
        play_btn->activate();
    }
    selection_info->redraw();
}

void CaptureWindow::on_play(Fl_Widget*, void* data){
    CaptureWindow* self = static_cast<CaptureWindow*>(data);
    if (self->player && self->player->playing()) self->stop_playing();
    else self->play();
}

void CaptureWindow::play(){
    const auto [begin, end] = waveform->selection();
    if (begin == end) return;
    try {
        if (!player) player = std::make_unique<ClipPlayer>();
        player->play(std::span(audio).subspan(begin, end - begin), rate);
    } catch (const std::exception& e){
        fl_alert("%s", e.what());
        return;
    }
    play_btn->label("Stop");
    play_btn->tooltip("Stop playing (Space)");
    Fl::add_timeout(PLAY_TICK, on_play_tick, this);
}

void CaptureWindow::stop_playing(){
    Fl::remove_timeout(on_play_tick, this);
    if (player) player->stop();
    waveform->hide_playhead();
    play_btn->label("Play");
    play_btn->tooltip("Play the selection (Space)");
}

void CaptureWindow::on_play_tick(void* data){
    CaptureWindow* self = static_cast<CaptureWindow*>(data);
    if (!self->player->playing()){
        self->stop_playing();
        return;
    }
    self->waveform->playhead(self->waveform->selection().first + self->player->position());
    Fl::repeat_timeout(PLAY_TICK, on_play_tick, data);
}

void CaptureWindow::on_close(Fl_Widget*, void* data){
    CaptureWindow* self = static_cast<CaptureWindow*>(data);
    self->stop_playing();
    self->win.hide();
    // Not kept any longer than it's needed
    self->waveform->audio({}, self->rate);
    self->audio.clear();
    self->audio.shrink_to_fit();
}
