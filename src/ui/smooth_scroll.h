#pragma once

// FLTK
#include <FL/Fl_Scroll.H>

// Vertical-only Fl_Scroll whose mouse wheel glides to the new position
// instead of jumping a whole step at once. Dragging the scrollbar still
// moves it directly.
//
// It draws its own thin scrollbar, just a handle on a trough: Fl_Scroll's
// built-in one always has arrow buttons, and their boxes don't line up
// with the arrows at small sizes.
//
// Add children like for any Fl_Group: the constructor calls begin(), so
// call end() after creating them.
class SmoothScroll : public Fl_Scroll {
public:
    SmoothScroll(int x, int y, int w, int h);
    ~SmoothScroll() override;

    int handle(int event) override;
    void draw() override;

    // Jumps to the top, e.g. when new content is shown
    void scroll_to_top();

    // Width a child can use without ending up under the scrollbar
    int content_w() const;

private:
    // Where the current glide is heading, as a yposition()
    int target_y = 0;
    bool gliding = false;

    // While the scrollbar's handle is being dragged
    bool dragging = false;
    // Where in the handle it was grabbed, from its top
    int grab_offset = 0;

    // Furthest yposition() that still has content at the bottom
    int max_y() const;
    void stop_glide();

    // Left edge of the scrollbar, along the right edge inside the box
    int bar_x() const;
    // Top and height of the handle; only meaningful when max_y() > 0
    void handle_box(int& top, int& height) const;
    // Scrolls so the handle's top ends up at handle_top
    void drag_to(int handle_top);

    // One animation frame. Static because Fl::add_timeout() takes a plain
    // function; the SmoothScroll comes in through data.
    static void on_tick(void* data);
};
