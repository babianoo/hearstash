#include "ui/waveform.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>

// FLTK
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>

#include "ui/theme.h"

// Height of the time axis under the waveform
static constexpr int AXIS_H = 18;
// How close to an edge of the selection, in pixels, grabs it
static constexpr int GRAB = 6;
// A drag shorter than this, in seconds, is a click: it keeps the selection
// there was
static constexpr double MIN_SELECTION = 0.05;
// Quieter audio than this is drawn at this size, so silence with a little
// noise doesn't fill the height
static constexpr float MIN_PEAK = 0.05f;
// Fewest pixels between the time axis's labels
static constexpr int MIN_TICK_GAP = 60;

Waveform::Waveform(int x, int y, int w, int h) : Fl_Widget(x, y, w, h){
    box(FL_FLAT_BOX);
    color(fl_lighter(FL_BACKGROUND_COLOR));
}

void Waveform::audio(std::span<const float> new_samples, int new_rate){
    samples = new_samples;
    rate = new_rate;
    sel_begin = sel_end = 0;
    playhead_at = static_cast<size_t>(-1);
    drag = Drag::None;
    compute_columns();
    redraw();
}

void Waveform::selection(size_t begin, size_t end){
    sel_begin = std::min(begin, samples.size());
    sel_end = std::clamp(end, sel_begin, samples.size());
    redraw();
}

void Waveform::playhead(size_t sample){
    if (sample == playhead_at) return;
    playhead_at = sample;
    redraw();
}

void Waveform::resize(int x, int y, int w, int h){
    const bool wider = w != this->w();
    Fl_Widget::resize(x, y, w, h);
    if (wider) compute_columns();
}

int Waveform::wave_h() const {
    return h() - AXIS_H;
}

void Waveform::compute_columns(){
    columns.assign(static_cast<size_t>(std::max(w(), 0)), {0.0f, 0.0f});
    if (samples.empty() || columns.empty()) return;

    float peak = MIN_PEAK;
    for (float s : samples) peak = std::max(peak, std::fabs(s));

    for (size_t c = 0; c < columns.size(); ++c){
        // At least one sample per column, even when zoomed in that far
        const size_t from = c * samples.size() / columns.size();
        const size_t to = std::max(from + 1, (c + 1) * samples.size() / columns.size());
        const auto [lo, hi] = std::minmax_element(samples.begin() + static_cast<std::ptrdiff_t>(from),
                                                  samples.begin() + static_cast<std::ptrdiff_t>(std::min(to, samples.size())));
        columns[c] = {*lo / peak, *hi / peak};
    }
}

int Waveform::x_of(size_t sample) const {
    if (samples.empty()) return x();
    return x() + static_cast<int>(static_cast<double>(sample) * w() / static_cast<double>(samples.size()));
}

size_t Waveform::sample_at(int px) const {
    if (w() <= 0) return 0;
    const double fraction = std::clamp(static_cast<double>(px - x()) / w(), 0.0, 1.0);
    return static_cast<size_t>(fraction * static_cast<double>(samples.size()));
}

Waveform::Drag Waveform::edge_at(int px) const {
    if (sel_begin == sel_end) return Drag::None;
    const int begin_x = x_of(sel_begin), end_x = x_of(sel_end);
    // The nearer one, when the selection is only a few pixels wide
    const int to_begin = std::abs(px - begin_x), to_end = std::abs(px - end_x);
    if (to_end <= GRAB && to_end <= to_begin) return Drag::End;
    if (to_begin <= GRAB) return Drag::Begin;
    return Drag::None;
}

int Waveform::handle(int event){
    switch (event){
    case FL_ENTER:
    case FL_MOVE:
        if (drag == Drag::None)
            window()->cursor(edge_at(Fl::event_x()) == Drag::None ? FL_CURSOR_DEFAULT : FL_CURSOR_WE);
        return 1;    // Needed to get FL_MOVE and FL_LEAVE
    case FL_LEAVE:
        if (drag == Drag::None) window()->cursor(FL_CURSOR_DEFAULT);
        return 1;

    case FL_PUSH: {
        if (Fl::event_button() != FL_LEFT_MOUSE || samples.empty()) return 0;
        drag_from = selection();
        drag = edge_at(Fl::event_x());
        if (drag == Drag::None){
            drag = Drag::New;
            drag_anchor = sample_at(Fl::event_x());
        }
        return 1;    // Needed to get FL_DRAG and FL_RELEASE
    }
    case FL_DRAG: {
        const size_t at = sample_at(Fl::event_x());
        switch (drag){
        case Drag::None: return 1;
        case Drag::New:
            // Only once it's long enough, so a click doesn't lose the
            // selection there was
            if (std::max(at, drag_anchor) - std::min(at, drag_anchor) < MIN_SELECTION * rate) return 1;
            sel_begin = std::min(at, drag_anchor);
            sel_end = std::max(at, drag_anchor);
            break;
        case Drag::Begin:
            sel_begin = at;
            break;
        case Drag::End:
            sel_end = at;
            break;
        }
        // An edge dragged past the other one becomes that one
        if (sel_begin > sel_end){
            std::swap(sel_begin, sel_end);
            drag = drag == Drag::Begin ? Drag::End : Drag::Begin;
        }
        redraw();
        return 1;
    }
    case FL_RELEASE:
        if (drag == Drag::None) return 1;
        drag = Drag::None;
        window()->cursor(edge_at(Fl::event_x()) == Drag::None ? FL_CURSOR_DEFAULT : FL_CURSOR_WE);
        if (selection_listener && selection() != drag_from) selection_listener();
        return 1;
    }
    return Fl_Widget::handle(event);
}

void Waveform::draw(){
    draw_box();
    const int wh = wave_h();
    const int mid = y() + wh / 2;
    const int half = wh / 2 - 4;

    // Behind the waveform
    const bool selected = sel_begin != sel_end;
    const int begin_x = x_of(sel_begin), end_x = x_of(sel_end);
    if (selected){
        fl_color(fl_color_average(ACCENT, color(), 0.18f));
        fl_rectf(begin_x, y(), end_x - begin_x, wh);
    }

    // The center line, so silence still shows as something
    fl_color(fl_color_average(DIM, color(), 0.3f));
    fl_xyline(x(), mid, x() + w() - 1);

    for (size_t c = 0; c < columns.size(); ++c){
        const int cx = x() + static_cast<int>(c);
        const bool in = selected && cx >= begin_x && cx < end_x;
        fl_color(in ? ACCENT : fl_color_average(DIM, color(), 0.6f));
        const auto [lo, hi] = columns[c];
        // Higher samples are drawn higher up, so hi gives the top
        fl_yxline(cx, mid - static_cast<int>(hi * half), mid - static_cast<int>(lo * half));
    }

    if (selected){
        fl_color(ACCENT);
        fl_line_style(FL_SOLID, 2);
        fl_yxline(begin_x, y(), y() + wh - 1);
        fl_yxline(end_x - 1, y(), y() + wh - 1);
        fl_line_style(0);
    }

    if (playhead_at < samples.size()){
        fl_color(FL_FOREGROUND_COLOR);
        fl_yxline(x_of(playhead_at), y(), y() + wh - 1);
    }

    draw_axis(y() + wh);
}

// Ticks every whole number of seconds, counted back from "now" on the
// right, as far apart as needed to fit their labels
void Waveform::draw_axis(int axis_y){
    fl_color(FL_BACKGROUND_COLOR);
    fl_rectf(x(), axis_y, w(), AXIS_H);
    if (samples.empty()) return;

    const double seconds = static_cast<double>(samples.size()) / rate;
    const double pixels_per_second = w() / seconds;
    int step = 1;
    for (int candidate : {1, 2, 5, 10, 15, 30, 60}){
        step = candidate;
        if (candidate * pixels_per_second >= MIN_TICK_GAP) break;
    }

    fl_font(FL_HELVETICA, 11);
    fl_color(DIM);
    for (int ago = 0; ago <= seconds; ago += step){
        const int tx = x() + w() - 1 - static_cast<int>(ago * pixels_per_second);
        fl_yxline(tx, axis_y, axis_y + 3);
        const std::string label = ago == 0 ? "now" : std::format("-{} s", ago);
        const int label_w = static_cast<int>(fl_width(label.c_str()));
        // "now" ends at the edge; the others are centered on their tick,
        // unless that would cut one off on the left
        const int lx = ago == 0 ? tx - label_w : std::max(x(), tx - label_w / 2);
        fl_draw(label.c_str(), lx, axis_y + AXIS_H - 4);
    }
}
