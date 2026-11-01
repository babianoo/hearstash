#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <utility>
#include <vector>

// FLTK
#include <FL/Fl_Widget.H>

// Draws audio as a waveform, with a selection on it and a time axis under
// it in seconds, ending at "now" on the right.
//
// For each pixel column it draws a vertical line from the lowest sample
// in that slice of the audio to the highest. Those are worked out when the
// audio or the width changes, not on every redraw.
//
// Dragging on it makes a new selection. Dragging near an edge of the
// selection moves that edge instead. A playhead can be shown on it, e.g.
// while the selection plays.
class Waveform : public Fl_Widget {
public:
    Waveform(int x, int y, int w, int h);

    // Shows samples, which are rate a second. Only borrows them: they have
    // to live until the next call, or the widget dies. Clears the selection
    // and the playhead.
    void audio(std::span<const float> samples, int rate);

    // The selection as a sample range, begin up to end. Empty (begin ==
    // end) if there's none.
    std::pair<size_t, size_t> selection() const { return {sel_begin, sel_end}; }
    // Sets it, clamped to the audio, and doesn't call on_selection
    void selection(size_t begin, size_t end);
    // Called when the user changes the selection, at the end of a drag
    // that changed it
    void on_selection(std::function<void()> listener) { selection_listener = std::move(listener); }

    // Draws a line at sample, or none if it's past the end
    void playhead(size_t sample);
    void hide_playhead() { playhead(static_cast<size_t>(-1)); }

    void resize(int x, int y, int w, int h) override;
    int handle(int event) override;

protected:
    void draw() override;

private:
    std::span<const float> samples;
    int rate = 1;
    // The lowest and highest sample under each pixel column
    std::vector<std::pair<float, float>> columns;

    size_t sel_begin = 0, sel_end = 0;
    size_t playhead_at = static_cast<size_t>(-1);
    std::function<void()> selection_listener;

    // What a drag is doing
    enum class Drag { None, New, Begin, End };
    Drag drag = Drag::None;
    // Where a new selection's drag started
    size_t drag_anchor = 0;
    // The selection when the drag started, to tell whether it changed
    std::pair<size_t, size_t> drag_from;

    // The part above the time axis
    int wave_h() const;
    void compute_columns();
    // Where sample is across the widget, and the other way round
    int x_of(size_t sample) const;
    size_t sample_at(int px) const;
    // Which edge of the selection, if any, is close enough to px to grab
    Drag edge_at(int px) const;
    void draw_axis(int axis_y);
};
