#pragma once

#include <functional>
#include <memory>
#include <utility>

// FLTK
#include <FL/Fl_Widget.H>

#include "core/image.h"

class Fl_RGB_Image;

// Shows a screenshot scaled to fit, with a rectangle on it to crop it to,
// e.g. just the video when the browser or desktop is around it. What's
// outside the rectangle is dimmed.
//
// Dragging inside the rectangle moves it, dragging its edges or corners
// resizes it, and dragging outside it draws a new one.
class CropView : public Fl_Widget {
public:
    CropView(int x, int y, int w, int h);
    // Defined in crop_view.cpp, where Fl_RGB_Image is complete
    ~CropView() override;

    // Shows image, cropped to all of it. Only borrows it: it has to live
    // until the next call, or the widget dies. nullptr shows nothing.
    void image(const Image* image);

    // The crop, in the image's pixels
    Rect crop() const { return rect; }
    // Sets it, clamped to the image, and doesn't call on_change
    void crop(Rect rect);
    // Called when the user changes the crop, at the end of a drag
    void on_change(std::function<void()> listener) { change_listener = std::move(listener); }

    void resize(int x, int y, int w, int h) override;
    int handle(int event) override;

protected:
    void draw() override;

private:
    const Image* source = nullptr;
    Rect rect;
    std::function<void()> change_listener;

    // The image made small enough to fit, and a darker copy for outside
    // the crop. Made when the image or the size changes, not on every
    // redraw. The Fl_RGB_Images only borrow the pixels.
    Image shown, dimmed;
    std::unique_ptr<Fl_RGB_Image> shown_image, dimmed_image;
    // Where shown is drawn, centered
    int image_x = 0, image_y = 0;

    // What a drag is doing: moving the crop, drawing a new one, or moving
    // some of its edges, as a mix of the Edge bits
    enum Edge { LEFT = 1, RIGHT = 2, TOP = 4, BOTTOM = 8 };
    enum class Drag { None, Move, New, Edges };
    Drag drag = Drag::None;
    int edges = 0;
    // The crop and the mouse, in the image's pixels, when the drag started
    Rect drag_from;
    int drag_x = 0, drag_y = 0;

    void make_shown();
    // Image pixels per pixel on screen
    double scale() const;
    // From the widget's pixels to the image's, clamped to the image
    std::pair<int, int> to_image(int px, int py) const;
    // The crop on screen
    Rect on_screen() const;
    // Which edges of the crop are close enough to grab at the point
    int edges_at(int px, int py) const;
    void set_cursor(int px, int py);
};
