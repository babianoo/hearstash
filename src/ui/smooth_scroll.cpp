#include "ui/smooth_scroll.h"

#include <algorithm>
#include <cstdlib>

// FLTK
#include <FL/Fl.H>

// Pixels scrolled per wheel notch, about 1 line of text
static constexpr int WHEEL_STEP = 8;
// Seconds between animation frames (60 fps)
static constexpr double TICK = 1.0 / 60;

SmoothScroll::SmoothScroll(int x, int y, int w, int h) : Fl_Scroll(x, y, w, h){
    type(Fl_Scroll::VERTICAL);
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

    return Fl_Scroll::handle(event);
}

void SmoothScroll::scroll_to_top(){
    stop_glide();
    scroll_to(0, 0);
}

int SmoothScroll::content_w() const {
    const int bar_w = scrollbar_size() ? scrollbar_size() : Fl::scrollbar_size();
    return w() - Fl::box_dw(box()) - bar_w;
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
