#include "ui/smooth_scroll.h"

#include <algorithm>
#include <cstdlib>

// FLTK
#include <FL/Fl.H>
#include <FL/fl_draw.H>

// Pixels scrolled per wheel notch, about 1 line of text
static constexpr int WHEEL_STEP = 8;
// Seconds between animation frames (60 fps)
static constexpr double TICK = 1.0 / 60;

// Scrollbar width, and the shortest the handle gets so it stays easy to grab
static constexpr int BAR_W = 8;
static constexpr int MIN_HANDLE_H = 24;
static const Fl_Color HANDLE_COLOR = fl_rgb_color(160, 160, 160);
static const Fl_Color HANDLE_DRAG_COLOR = fl_rgb_color(120, 120, 120);

SmoothScroll::SmoothScroll(int x, int y, int w, int h) : Fl_Scroll(x, y, w, h){
    // No built-in scrollbars; draw() and handle() do the vertical one
    type(0);
    // A real box, not a frame: it fills the space the children don't cover
    box(FL_DOWN_BOX);
    color(FL_BACKGROUND2_COLOR);
}

SmoothScroll::~SmoothScroll(){
    // A pending tick would otherwise run on a deleted widget
    Fl::remove_timeout(on_tick, this);
}

int SmoothScroll::handle(int event){
    if (event == FL_MOUSEWHEEL && Fl::event_dy() != 0){
        // Spinning the wheel again mid-glide adds to where the glide is
        // heading, not to where the view is right now, so fast spins add up.
        const int from = gliding ? target_y : yposition();
        target_y = std::clamp(from + Fl::event_dy() * WHEEL_STEP, 0, max_y());

        if (!gliding && target_y != yposition()){
            gliding = true;
            Fl::add_timeout(TICK, on_tick, this);
        }
        return 1;
    }

    // A click (e.g. on the scrollbar) takes over from a glide in progress
    if (event == FL_PUSH) stop_glide();

    if (event == FL_PUSH && Fl::event_x() >= bar_x() && max_y() > 0){
        int top, height;
        handle_box(top, height);
        // Grabbing the handle keeps it where it is under the mouse; a click
        // on the trough centers it on the mouse first
        if (Fl::event_y() >= top && Fl::event_y() < top + height){
            grab_offset = Fl::event_y() - top;
        } else {
            grab_offset = height / 2;
            drag_to(Fl::event_y() - grab_offset);
        }
        dragging = true;
        redraw();
        return 1;
    }
    if (dragging && event == FL_DRAG){
        drag_to(Fl::event_y() - grab_offset);
        return 1;
    }
    if (dragging && event == FL_RELEASE){
        dragging = false;
        redraw();
        return 1;
    }

    return Fl_Scroll::handle(event);
}

void SmoothScroll::draw(){
    Fl_Scroll::draw();
    if (max_y() == 0) return;

    // The whole strip, since scrolling just copied it along with the content
    fl_color(color());
    fl_rectf(bar_x(), y() + Fl::box_dy(box()), BAR_W, h() - Fl::box_dh(box()));

    int top, height;
    handle_box(top, height);
    fl_draw_box(FL_RFLAT_BOX, bar_x(), top, BAR_W, height,
                dragging ? HANDLE_DRAG_COLOR : HANDLE_COLOR);
}

void SmoothScroll::scroll_to_top(){
    stop_glide();
    scroll_to(0, 0);
}

int SmoothScroll::content_w() const {
    return w() - Fl::box_dw(box()) - BAR_W;
}

int SmoothScroll::max_y() const {
    // Children are placed in window coordinates, shifted up by yposition()
    const int content_top = y() + Fl::box_dy(box()) - yposition();
    int content_bottom = content_top;
    for (int i = 0; i < children(); ++i){
        const Fl_Widget* c = child(i);
        // The two scrollbars are children too, but they aren't content
        if (c == &scrollbar || c == &hscrollbar) continue;
        content_bottom = std::max(content_bottom, c->y() + c->h());
    }

    const int visible_h = h() - Fl::box_dh(box());
    return std::max(0, content_bottom - content_top - visible_h);
}

int SmoothScroll::bar_x() const {
    const int right_border = Fl::box_dw(box()) - Fl::box_dx(box());
    return x() + w() - right_border - BAR_W;
}

void SmoothScroll::handle_box(int& top, int& height) const {
    const int track_y = y() + Fl::box_dy(box());
    const int track_h = h() - Fl::box_dh(box());
    const int max = max_y();

    // As tall, relative to the track, as the view is relative to the content
    height = std::clamp(track_h * track_h / (track_h + max), MIN_HANDLE_H, track_h);
    top = track_y + (track_h - height) * std::min(yposition(), max) / max;
}

void SmoothScroll::drag_to(int handle_top){
    int top, height;
    handle_box(top, height);
    const int track_y = y() + Fl::box_dy(box());
    const int travel = h() - Fl::box_dh(box()) - height;
    if (travel <= 0) return;

    const int max = max_y();
    const int pos = std::clamp((handle_top - track_y) * max / travel, 0, max);
    scroll_to(xposition(), pos);
}

void SmoothScroll::stop_glide(){
    gliding = false;
    Fl::remove_timeout(on_tick, this);
}

void SmoothScroll::on_tick(void* data){
    SmoothScroll* self = static_cast<SmoothScroll*>(data);

    // The content may have shrunk since the glide started
    self->target_y = std::min(self->target_y, self->max_y());
    const int left = self->target_y - self->yposition();

    if (std::abs(left) <= 1){
        self->scroll_to(self->xposition(), self->target_y);
        self->gliding = false;
        return;
    }

    // Covers a quarter of the remaining distance per frame: fast at first,
    // then slowing down as it arrives. At least 1px so it always gets there.
    int step = left / 4;
    if (step == 0) step = left > 0 ? 1 : -1;
    self->scroll_to(self->xposition(), self->yposition() + step);

    Fl::repeat_timeout(TICK, on_tick, data);
}
