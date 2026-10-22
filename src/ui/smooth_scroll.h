#pragma once

// FLTK
#include <FL/Fl_Scroll.H>

// Vertical-only Fl_Scroll whose mouse wheel glides to the new position
// instead of jumping a whole step at once. Dragging the scrollbar still
// moves it directly.
//
// Add children like for any Fl_Group: the constructor calls begin(), so
// call end() after creating them.
class SmoothScroll : public Fl_Scroll {
public:
    SmoothScroll(int x, int y, int w, int h);
    ~SmoothScroll() override;

    int handle(int event) override;

    // Jumps to the top, e.g. when new content is shown
    void scroll_to_top();

    // Width a child can use without ending up under the scrollbar
    int content_w() const;

private:
    // Where the current glide is heading, as a yposition()
    int target_y = 0;
    bool gliding = false;

    // Furthest yposition() that still has content at the bottom
    int max_y() const;
    void stop_glide();

    // One animation frame. Static because Fl::add_timeout() takes a plain
    // function; the SmoothScroll comes in through data.
    static void on_tick(void* data);
};
