#include "ui/wrapped_text.h"

#include <algorithm>
#include <cctype>

// FLTK
#include <FL/fl_draw.H>
#include <FL/fl_utf8.h>

// Space between the text and the widget's edges
static constexpr int PAD = 6;

// Bytes in the UTF-8 character starting at s[i]. At least 1, so a broken
// byte still moves the loops below forward.
static size_t char_len(std::string_view s, size_t i){
    return std::min<size_t>(std::max(1, fl_utf8len1(s[i])), s.size() - i);
}

// Bytes of s that fit in max_w pixels: up to the last space that fits, or
// as many characters as fit if there's no such space. Always at least one
// character, so even a very narrow widget makes progress.
static size_t fit(std::string_view s, double max_w){
    if (fl_width(s.data(), static_cast<int>(s.size())) <= max_w) return s.size();

    size_t end = 0, last_space = 0;
    while (end < s.size()){
        const size_t next = end + char_len(s, end);
        if (fl_width(s.data(), static_cast<int>(next)) > max_w) break;
        if (s[end] == ' ') last_space = end;
        end = next;
    }

    if (last_space > 0) return last_space;
    return end > 0 ? end : char_len(s, 0);
}

WrappedText::WrappedText(int x, int y, int w) : Fl_Widget(x, y, w, 2 * PAD){
    // Draws its own background, since it can be redrawn without its parent
    box(FL_FLAT_BOX);
    color(FL_BACKGROUND2_COLOR);
}

void WrappedText::text(std::string_view new_text){
    full_text = new_text;
    rewrap();
    redraw();
}

void WrappedText::resize(int x, int y, int w, int h){
    // Scrolling moves this widget too, but only a new width changes the wrapping
    const bool width_changed = w != this->w();
    Fl_Widget::resize(x, y, w, h);
    if (width_changed && !full_text.empty()) rewrap();
}

void WrappedText::rewrap(){
    lines.clear();
    fl_font(labelfont(), labelsize());
    const int max_w = w() - 2 * PAD;

    std::string_view rest_of_text = full_text;
    while (true){
        const size_t newline = rest_of_text.find('\n');
        std::string_view paragraph = rest_of_text.substr(0, newline);

        const size_t start = paragraph.find_first_not_of(' ');
        if (start == std::string_view::npos){
            lines.push_back({});    // Empty line
        } else {
            // Leading spaces become an indent, so a wrap can't break there
            int indent = static_cast<int>(fl_width(paragraph.data(), static_cast<int>(start)));

            // Wrapped lines line up after the "1. " of a numbered line
            size_t hang_end = start;
            while (hang_end < paragraph.size() && std::isdigit(static_cast<unsigned char>(paragraph[hang_end])))
                ++hang_end;
            if (hang_end > start && paragraph.substr(hang_end, 2) == ". ") hang_end += 2;
            else hang_end = start;
            const int hang = static_cast<int>(fl_width(paragraph.data(), static_cast<int>(hang_end)));

            std::string_view rest = paragraph.substr(start);
            while (!rest.empty()){
                const size_t n = fit(rest, max_w - indent);
                lines.push_back({std::string(rest.substr(0, n)), indent});
                rest.remove_prefix(n);
                // The space it broke at doesn't start the next line
                while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
                indent = hang;
            }
        }

        if (newline == std::string_view::npos) break;
        rest_of_text.remove_prefix(newline + 1);
    }

    // No blank space under the last line
    while (!lines.empty() && lines.back().text.empty()) lines.pop_back();

    // Fl_Widget's resize, so this doesn't call itself
    Fl_Widget::resize(x(), y(), w(), 2 * PAD + static_cast<int>(lines.size()) * fl_height());
}

void WrappedText::draw(){
    draw_box();

    fl_font(labelfont(), labelsize());
    fl_color(active_r() ? labelcolor() : fl_inactive(labelcolor()));
    const int line_h = fl_height();

    for (size_t i = 0; i < lines.size(); ++i){
        const int line_y = y() + PAD + static_cast<int>(i) * line_h;
        // Most lines are scrolled out of view; skip those
        if (!fl_not_clipped(x(), line_y, w(), line_h)) continue;

        // This fl_draw() draws the text as is: no '@' symbols, no '&' underlines
        const Line& line = lines[i];
        fl_draw(line.text.data(), static_cast<int>(line.text.size()),
                x() + PAD + line.indent, line_y + line_h - fl_descent());
    }
}
