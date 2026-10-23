#include "ui/wrapped_text.h"

#include <algorithm>
#include <cctype>

// FLTK
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
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
    selection_color(FL_SELECTION_COLOR);    // Same highlight as text fields
}

void WrappedText::text(std::string_view new_text){
    full_text = new_text;
    // Its offsets were into the old text
    sel_anchor = sel_cursor = 0;
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
        // Offset in full_text of where a view into it starts
        auto offset = [&](std::string_view view){
            return static_cast<size_t>(view.data() - full_text.data());
        };

        if (start == std::string_view::npos){
            lines.push_back({{}, 0, offset(paragraph)});    // Empty line
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
                lines.push_back({std::string(rest.substr(0, n)), indent, offset(rest)});
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

size_t WrappedText::offset_at(int mouse_x, int mouse_y){
    if (lines.empty()) return 0;

    fl_font(labelfont(), labelsize());
    const int line_h = fl_height();

    // Above the text is its start, below it is its end
    if (mouse_y < y() + PAD) return lines.front().start;
    const size_t i = static_cast<size_t>((mouse_y - y() - PAD) / line_h);
    if (i >= lines.size()) return lines.back().start + lines.back().text.size();

    // The first character whose middle is right of the mouse. Left of the
    // text is the line's start, right of it the line's end.
    const Line& line = lines[i];
    const double mouse = mouse_x - (x() + PAD + line.indent);
    size_t end = 0;
    double left = 0;
    while (end < line.text.size()){
        const size_t next = end + char_len(line.text, end);
        const double right = fl_width(line.text.data(), static_cast<int>(next));
        if (mouse < (left + right) / 2) break;
        end = next;
        left = right;
    }
    return line.start + end;
}

void WrappedText::copy_selection(int destination) const {
    const size_t sel_lo = std::min(sel_anchor, sel_cursor), sel_hi = std::max(sel_anchor, sel_cursor);
    if (sel_lo == sel_hi) return;
    // Straight from full_text, so wrapped lines come out as the one line they are
    Fl::copy(full_text.data() + sel_lo, static_cast<int>(sel_hi - sel_lo), destination);
}

int WrappedText::handle(int event){
    switch (event){
    case FL_ENTER:
        window()->cursor(FL_CURSOR_INSERT);    // The I-beam, like over a text field
        return 1;    // Needed to get FL_LEAVE too
    case FL_LEAVE:
        window()->cursor(FL_CURSOR_DEFAULT);
        return 1;

    case FL_PUSH:
        if (Fl::event_button() != FL_LEFT_MOUSE) return 0;
        take_focus();    // So Ctrl+C comes here
        sel_anchor = sel_cursor = offset_at(Fl::event_x(), Fl::event_y());
        redraw();
        return 1;    // Needed to get FL_DRAG and FL_RELEASE too
    case FL_DRAG: {
        const size_t cursor = offset_at(Fl::event_x(), Fl::event_y());
        if (cursor != sel_cursor){
            sel_cursor = cursor;
            redraw();
        }
        return 1;
    }
    case FL_RELEASE:
        copy_selection(0);
        return 1;

    case FL_FOCUS:
    case FL_UNFOCUS:
        return 1;    // Accepts keyboard focus
    case FL_KEYBOARD:
        if (!Fl::event_state(FL_COMMAND)) return 0;
        if (Fl::event_key() == 'c'){
            copy_selection(1);
            return 1;
        }
        if (Fl::event_key() == 'a'){
            sel_anchor = 0;
            sel_cursor = full_text.size();
            copy_selection(0);
            redraw();
            return 1;
        }
        return 0;
    }
    return Fl_Widget::handle(event);
}

void WrappedText::draw(){
    draw_box();

    fl_font(labelfont(), labelsize());
    const Fl_Color text_color = active_r() ? labelcolor() : fl_inactive(labelcolor());
    // Readable on the highlight, e.g. white on dark blue
    const Fl_Color selected_color = fl_contrast(text_color, selection_color());
    const int line_h = fl_height();
    const size_t sel_lo = std::min(sel_anchor, sel_cursor), sel_hi = std::max(sel_anchor, sel_cursor);

    for (size_t i = 0; i < lines.size(); ++i){
        const int line_y = y() + PAD + static_cast<int>(i) * line_h;
        // Most lines are scrolled out of view; skip those
        if (!fl_not_clipped(x(), line_y, w(), line_h)) continue;

        // This fl_draw() draws the text as is: no '@' symbols, no '&' underlines
        const Line& line = lines[i];
        const char* text = line.text.data();
        const int len = static_cast<int>(line.text.size());
        const int text_x = x() + PAD + line.indent, baseline = line_y + line_h - fl_descent();
        fl_color(text_color);
        fl_draw(text, len, text_x, baseline);

        // The selected part of this line, if any
        const size_t line_end = line.start + line.text.size();
        if (sel_lo == sel_hi || sel_lo > line_end || sel_hi <= line.start) continue;
        const int from = static_cast<int>(std::max(sel_lo, line.start) - line.start);
        const int to = static_cast<int>(std::min(sel_hi, line_end) - line.start);
        const int sel_x = text_x + static_cast<int>(fl_width(text, from));
        int sel_w = static_cast<int>(fl_width(text, to)) - static_cast<int>(fl_width(text, from));
        // A space's worth more when the selection goes on to the next line,
        // so a selected empty line shows up too
        if (sel_hi > line_end) sel_w += static_cast<int>(fl_width(' '));

        // Highlight, then the text again on top of it, but only inside it
        fl_color(selection_color());
        fl_rectf(sel_x, line_y, sel_w, line_h);
        fl_push_clip(sel_x, line_y, sel_w, line_h);
        fl_color(selected_color);
        fl_draw(text, len, text_x, baseline);
        fl_pop_clip();
    }
}
