#include "ui/wrapped_text.h"

#include <algorithm>
#include <cctype>

// FLTK
#include <FL/Fl.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>
#include <FL/fl_utf8.h>

#include "ui/clipboard.h"

// Space between the text and the widget's edges
static constexpr int PAD = 6;

// Bytes in the UTF-8 character starting at s[i]. At least 1, so a broken
// byte still moves the loops below forward.
static size_t char_len(std::string_view s, size_t i){
    return std::min<size_t>(std::max(1, fl_utf8len1(s[i])), s.size() - i);
}

static void set_font(const TextStyle& style){
    fl_font(style.font, style.size);
}

WrappedText::WrappedText(int x, int y, int w) : Fl_Widget(x, y, w, 2 * PAD){
    // Draws its own background, since it can be redrawn without its parent
    box(FL_FLAT_BOX);
    color(FL_BACKGROUND2_COLOR);
    selection_color(FL_SELECTION_COLOR);    // Same highlight as text fields
}

void WrappedText::text(const std::vector<TextSpan>& spans){
    full_text.clear();
    runs.clear();
    for (const TextSpan& span : spans){
        if (span.text.empty()) continue;
        full_text += span.text;
        runs.push_back({full_text.size(), span.style});
    }
    if (runs.empty()) runs.push_back({0, {}});

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
    const int max_w = w() - 2 * PAD;

    size_t para_start = 0;
    while (true){
        const size_t newline = full_text.find('\n', para_start);
        const size_t para_end = newline == std::string::npos ? full_text.size() : newline;

        const size_t start = full_text.find_first_not_of(' ', para_start);
        if (start >= para_end){
            lines.push_back({.start = para_start, .end = para_start});    // Empty line
        } else {
            // Leading spaces become an indent, so a wrap can't break there
            int indent = static_cast<int>(width(para_start, start));

            // Wrapped lines line up after the "1. " of a numbered line
            size_t hang_end = start;
            while (hang_end < para_end && std::isdigit(static_cast<unsigned char>(full_text[hang_end])))
                ++hang_end;
            if (hang_end > start && full_text.compare(hang_end, 2, ". ") == 0) hang_end += 2;
            else hang_end = start;
            const int hang = static_cast<int>(width(para_start, hang_end));

            size_t pos = start;
            while (pos < para_end){
                const size_t end = fit(pos, para_end, max_w - indent);
                lines.push_back({.start = pos, .end = end, .indent = indent});
                pos = end;
                // The space it broke at doesn't start the next line
                while (pos < para_end && full_text[pos] == ' ') ++pos;
                indent = hang;
            }
        }

        if (newline == std::string::npos) break;
        para_start = newline + 1;
    }

    // No blank space under the last line
    while (!lines.empty() && lines.back().start == lines.back().end) lines.pop_back();

    int text_h = 0;
    for (Line& line : lines){
        measure(line);
        line.y = text_h;
        text_h += line.h;
    }

    // Fl_Widget's resize, so this doesn't call itself
    Fl_Widget::resize(x(), y(), w(), 2 * PAD + text_h);
}

size_t WrappedText::run_at(size_t offset) const {
    const auto it = std::upper_bound(runs.begin(), runs.end(), offset,
                                     [](size_t o, const Run& run){ return o < run.end; });
    return it == runs.end() ? runs.size() - 1 : static_cast<size_t>(it - runs.begin());
}

double WrappedText::width(size_t from, size_t to) const {
    double total = 0;
    for (size_t r = run_at(from); from < to; ++r){
        const size_t piece_end = std::min(to, runs[r].end);
        set_font(runs[r].style);
        total += fl_width(full_text.data() + from, static_cast<int>(piece_end - from));
        from = piece_end;
    }
    return total;
}

// Up to the last space that fits, or as many characters as fit if there's
// no such space. Always at least one character, so even a very narrow
// widget makes progress.
size_t WrappedText::fit(size_t from, size_t to, double max_w) const {
    if (width(from, to) <= max_w) return to;

    // Measures from where the current run starts, plus the finished runs
    // before it, instead of from `from` each time, which would cost a
    // font switch per run per character
    size_t end = from, last_space = from;
    size_t r = run_at(from), run_start = from;
    double runs_before_w = 0;
    set_font(runs[r].style);
    while (end < to){
        while (end >= runs[r].end && r + 1 < runs.size()){
            runs_before_w += fl_width(full_text.data() + run_start, static_cast<int>(end - run_start));
            run_start = end;
            set_font(runs[++r].style);
        }
        const size_t next = end + char_len(full_text, end);
        if (runs_before_w + fl_width(full_text.data() + run_start, static_cast<int>(next - run_start)) > max_w)
            break;
        if (full_text[end] == ' ') last_space = end;
        end = next;
    }

    if (last_space > from) return last_space;
    return end > from ? end : from + char_len(full_text, from);
}

void WrappedText::measure(Line& line) const {
    int ascent = 0, descent = 0;
    // An empty line is as tall as the text it would have
    const size_t last = line.end > line.start ? run_at(line.end - 1) : run_at(line.start);
    for (size_t r = run_at(line.start); r <= last; ++r){
        set_font(runs[r].style);
        ascent = std::max(ascent, fl_height() - fl_descent());
        descent = std::max(descent, fl_descent());
    }
    line.h = ascent + descent;
    line.baseline = ascent;
}

size_t WrappedText::offset_at(int mouse_x, int mouse_y){
    if (lines.empty()) return 0;

    // Above the text is its start, below it is its end
    const int text_y = mouse_y - (y() + PAD);
    if (text_y < 0) return lines.front().start;
    const auto it = std::find_if(lines.begin(), lines.end(),
                                 [&](const Line& line){ return text_y < line.y + line.h; });
    if (it == lines.end()) return lines.back().end;

    // The first character whose middle is right of the mouse. Left of the
    // text is the line's start, right of it the line's end.
    const Line& line = *it;
    const double mouse = mouse_x - (x() + PAD + line.indent);
    size_t end = line.start;
    double left = 0;
    while (end < line.end){
        const size_t next = end + char_len(full_text, end);
        const double right = width(line.start, next);
        if (mouse < (left + right) / 2) break;
        end = next;
        left = right;
    }
    return end;
}

std::string WrappedText::selected_text() const {
    const size_t sel_lo = std::min(sel_anchor, sel_cursor), sel_hi = std::max(sel_anchor, sel_cursor);
    // Straight from full_text, so wrapped lines come out as the one line they are
    return full_text.substr(sel_lo, sel_hi - sel_lo);
}

void WrappedText::copy_selection(int destination) const {
    const std::string selected = selected_text();
    if (selected.empty()) return;
    if (destination == 1) clipboard_copy(selected);
    else Fl::copy(selected.data(), static_cast<int>(selected.size()), 0);
}

void WrappedText::select_all(){
    sel_anchor = 0;
    sel_cursor = full_text.size();
    copy_selection(0);
    redraw();
}

void WrappedText::show_menu(){
    // The last item stays empty: it ends the menu
    Fl_Menu_Item menu[3]{};
    menu[0].label(Fl_Input::copy_menu_text);    // Same label as text fields
    menu[1].label("Select All");
    if (sel_anchor == sel_cursor) menu[0].deactivate();
    if (full_text.empty()) menu[1].deactivate();

    fl_cursor(FL_CURSOR_DEFAULT);
    const Fl_Menu_Item* chosen = menu->popup(Fl::event_x(), Fl::event_y());
    if (chosen == &menu[0]) copy_selection(1);
    else if (chosen == &menu[1]) select_all();
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
        if (Fl::event_button() == FL_RIGHT_MOUSE){
            take_focus();
            show_menu();
            return 1;
        }
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
        if (select_listener && Fl::event_button() == FL_LEFT_MOUSE && sel_anchor != sel_cursor) select_listener();
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
            select_all();
            return 1;
        }
        return 0;
    }
    return Fl_Widget::handle(event);
}

void WrappedText::draw_line(const Line& line, int x, int baseline, bool on_selection) const {
    double piece_x = x;
    size_t from = line.start;
    for (size_t r = run_at(from); from < line.end; ++r){
        const size_t piece_end = std::min(line.end, runs[r].end);
        const char* piece = full_text.data() + from;
        const int len = static_cast<int>(piece_end - from);

        Fl_Color color = runs[r].style.color;
        if (!active_r()) color = fl_inactive(color);
        // Readable on the highlight, e.g. white on dark blue
        if (on_selection) color = fl_contrast(color, selection_color());
        set_font(runs[r].style);
        fl_color(color);
        // This fl_draw() draws the text as is: no '@' symbols, no '&' underlines
        fl_draw(piece, len, static_cast<int>(piece_x), baseline);

        piece_x += fl_width(piece, len);
        from = piece_end;
    }
}

void WrappedText::draw(){
    draw_box();

    const size_t sel_lo = std::min(sel_anchor, sel_cursor), sel_hi = std::max(sel_anchor, sel_cursor);

    for (const Line& line : lines){
        const int line_y = y() + PAD + line.y;
        // Most lines are scrolled out of view; skip those
        if (!fl_not_clipped(x(), line_y, w(), line.h)) continue;

        const int text_x = x() + PAD + line.indent, baseline = line_y + line.baseline;
        draw_line(line, text_x, baseline, false);

        // The selected part of this line, if any
        if (sel_lo == sel_hi || sel_lo > line.end || sel_hi <= line.start) continue;
        const size_t from = std::max(sel_lo, line.start), to = std::min(sel_hi, line.end);
        const int from_x = static_cast<int>(width(line.start, from));
        const int sel_x = text_x + from_x;
        int sel_w = static_cast<int>(width(line.start, to)) - from_x;
        // A space's worth more when the selection goes on to the next line,
        // so a selected empty line shows up too
        if (sel_hi > line.end){
            set_font(runs[run_at(line.end)].style);
            sel_w += static_cast<int>(fl_width(' '));
        }

        // Highlight, then the text again on top of it, but only inside it
        fl_color(selection_color());
        fl_rectf(sel_x, line_y, sel_w, line.h);
        fl_push_clip(sel_x, line_y, sel_w, line.h);
        draw_line(line, text_x, baseline, true);
        fl_pop_clip();
    }
}
