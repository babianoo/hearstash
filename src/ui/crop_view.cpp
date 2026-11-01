#include "ui/crop_view.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <tuple>

// FLTK
#include <FL/Fl.H>
#include <FL/Fl_Image.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>

#include "ui/theme.h"

// How close to an edge of the crop, in pixels on screen, grabs it
static constexpr int GRAB = 8;
// The smallest crop, in the image's pixels
static constexpr int MIN_SIZE = 16;
// How much of its brightness what's outside the crop keeps
static constexpr int DIM_PERCENT = 35;

CropView::CropView(int x, int y, int w, int h) : Fl_Widget(x, y, w, h){
    box(FL_FLAT_BOX);
    color(fl_darker(FL_BACKGROUND_COLOR));
}

CropView::~CropView() = default;

void CropView::image(const Image* image){
    source = image && !image->empty() ? image : nullptr;
    rect = source ? Rect{0, 0, source->width, source->height} : Rect{};
    drag = Drag::None;
    make_shown();
    redraw();
}

void CropView::crop(Rect r){
    if (!source) return;
    const int x0 = std::clamp(r.x, 0, source->width - 1), y0 = std::clamp(r.y, 0, source->height - 1);
    const int x1 = std::clamp(r.x + r.w, x0 + 1, source->width), y1 = std::clamp(r.y + r.h, y0 + 1, source->height);
    rect = {x0, y0, x1 - x0, y1 - y0};
    redraw();
}

void CropView::resize(int x, int y, int w, int h){
    const bool resized = w != this->w() || h != this->h();
    Fl_Widget::resize(x, y, w, h);
    if (resized) make_shown();
}

void CropView::make_shown(){
    shown_image.reset();
    dimmed_image.reset();
    if (!source) return;

    // As big as fits, keeping its shape
    const int height = std::min(h(), static_cast<int>(static_cast<long long>(w()) * source->height / source->width));
    shown = shrink_to_height(*source, std::max(height, 1));
    dimmed = shown;
    for (std::uint8_t& v : dimmed.rgb) v = static_cast<std::uint8_t>(v * DIM_PERCENT / 100);

    shown_image = std::make_unique<Fl_RGB_Image>(shown.rgb.data(), shown.width, shown.height, 3);
    dimmed_image = std::make_unique<Fl_RGB_Image>(dimmed.rgb.data(), dimmed.width, dimmed.height, 3);
    image_x = (w() - shown.width) / 2;
    image_y = (h() - shown.height) / 2;
}

double CropView::scale() const {
    return source && shown.width ? static_cast<double>(source->width) / shown.width : 1.0;
}

std::pair<int, int> CropView::to_image(int px, int py) const {
    const double s = scale();
    const int ix = static_cast<int>(std::lround((px - x() - image_x) * s));
    const int iy = static_cast<int>(std::lround((py - y() - image_y) * s));
    return {std::clamp(ix, 0, source->width), std::clamp(iy, 0, source->height)};
}

Rect CropView::on_screen() const {
    const double s = scale();
    const int left = x() + image_x + static_cast<int>(std::lround(rect.x / s));
    const int top = y() + image_y + static_cast<int>(std::lround(rect.y / s));
    const int right = x() + image_x + static_cast<int>(std::lround((rect.x + rect.w) / s));
    const int bottom = y() + image_y + static_cast<int>(std::lround((rect.y + rect.h) / s));
    return {left, top, right - left, bottom - top};
}

int CropView::edges_at(int px, int py) const {
    if (!source) return 0;
    const Rect r = on_screen();
    // Only near the rectangle, not anywhere along the lines through it
    if (px < r.x - GRAB || px > r.x + r.w + GRAB || py < r.y - GRAB || py > r.y + r.h + GRAB) return 0;
    int found = 0;
    if (std::abs(px - r.x) <= GRAB) found |= LEFT;
    else if (std::abs(px - (r.x + r.w)) <= GRAB) found |= RIGHT;
    if (std::abs(py - r.y) <= GRAB) found |= TOP;
    else if (std::abs(py - (r.y + r.h)) <= GRAB) found |= BOTTOM;
    return found;
}

void CropView::set_cursor(int px, int py){
    const int found = edges_at(px, py);
    const Rect r = on_screen();
    Fl_Cursor cursor = FL_CURSOR_CROSS;
    if (found == (LEFT | TOP) || found == (RIGHT | BOTTOM)) cursor = FL_CURSOR_NWSE;
    else if (found == (RIGHT | TOP) || found == (LEFT | BOTTOM)) cursor = FL_CURSOR_NESW;
    else if (found & (LEFT | RIGHT)) cursor = FL_CURSOR_WE;
    else if (found & (TOP | BOTTOM)) cursor = FL_CURSOR_NS;
    else if (px > r.x && px < r.x + r.w && py > r.y && py < r.y + r.h) cursor = FL_CURSOR_MOVE;
    window()->cursor(source ? cursor : FL_CURSOR_DEFAULT);
}

int CropView::handle(int event){
    switch (event){
    case FL_ENTER:
    case FL_MOVE:
        if (drag == Drag::None) set_cursor(Fl::event_x(), Fl::event_y());
        return 1;    // Needed to get FL_MOVE and FL_LEAVE
    case FL_LEAVE:
        if (drag == Drag::None) window()->cursor(FL_CURSOR_DEFAULT);
        return 1;

    case FL_PUSH: {
        if (Fl::event_button() != FL_LEFT_MOUSE || !source) return 0;
        drag_from = rect;
        std::tie(drag_x, drag_y) = to_image(Fl::event_x(), Fl::event_y());
        edges = edges_at(Fl::event_x(), Fl::event_y());
        const Rect r = on_screen();
        const bool inside = Fl::event_x() > r.x && Fl::event_x() < r.x + r.w
                         && Fl::event_y() > r.y && Fl::event_y() < r.y + r.h;
        drag = edges ? Drag::Edges : inside ? Drag::Move : Drag::New;
        return 1;    // Needed to get FL_DRAG and FL_RELEASE
    }
    case FL_DRAG: {
        if (drag == Drag::None) return 1;
        const auto [mx, my] = to_image(Fl::event_x(), Fl::event_y());
        const Rect& from = drag_from;
        switch (drag){
        case Drag::None: break;
        case Drag::Move:
            // Stops at the image's edges, keeping its size
            rect.x = std::clamp(from.x + mx - drag_x, 0, source->width - from.w);
            rect.y = std::clamp(from.y + my - drag_y, 0, source->height - from.h);
            break;
        case Drag::New: {
            // Only once it's big enough, so a click doesn't lose the crop
            if (std::abs(mx - drag_x) < MIN_SIZE || std::abs(my - drag_y) < MIN_SIZE) return 1;
            rect = {std::min(mx, drag_x), std::min(my, drag_y), std::abs(mx - drag_x), std::abs(my - drag_y)};
            break;
        }
        case Drag::Edges: {
            int left = from.x, top = from.y, right = from.x + from.w, bottom = from.y + from.h;
            if (edges & LEFT) left = std::min(mx, right - MIN_SIZE);
            if (edges & RIGHT) right = std::max(mx, left + MIN_SIZE);
            if (edges & TOP) top = std::min(my, bottom - MIN_SIZE);
            if (edges & BOTTOM) bottom = std::max(my, top + MIN_SIZE);
            rect = {left, top, right - left, bottom - top};
            break;
        }
        }
        crop(rect);
        return 1;
    }
    case FL_RELEASE:
        if (drag == Drag::None) return 1;
        drag = Drag::None;
        set_cursor(Fl::event_x(), Fl::event_y());
        if (change_listener && rect != drag_from) change_listener();
        return 1;
    }
    return Fl_Widget::handle(event);
}

void CropView::draw(){
    draw_box();
    if (!source){
        fl_font(FL_HELVETICA, 14);
        fl_color(DIM);
        fl_draw("No screenshot", x(), y(), w(), h(), FL_ALIGN_CENTER);
        return;
    }

    // All of it dimmed, then the crop over it as it is
    dimmed_image->draw(x() + image_x, y() + image_y);
    const Rect r = on_screen();
    fl_push_clip(r.x, r.y, r.w, r.h);
    shown_image->draw(x() + image_x, y() + image_y);
    fl_pop_clip();

    fl_color(ACCENT);
    fl_line_style(FL_SOLID, 2);
    fl_rect(r.x, r.y, r.w, r.h);
    fl_line_style(0);
}
