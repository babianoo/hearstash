#include "ui/history_table.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// FLTK
#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/fl_utf8.h>

#include "core/api.h"
#include "core/local_time.h"
#include "ui/smooth_scroll.h"
#include "ui/theme.h"

static constexpr int HEADER_H = 26;
static constexpr int ROW_H = 26;
// Space between a column's edge and its text
static constexpr int PAD = 8;
// The right two columns; Search gets the rest
static constexpr int TIME_W = 130;
static constexpr int API_W = 90;
// The API's badge in front of a search
static constexpr int BADGE = 16;
static constexpr int BADGE_GAP = 6;

// Indexed by Api: the badge's color and letter
static const Fl_Color BADGE_COLORS[] = {fl_rgb_color(60, 150, 70), fl_rgb_color(15, 43, 70),
                                        fl_rgb_color(120, 120, 120)};
static constexpr const char* BADGE_LETTERS[] = {"J", "D", "M"};

// Left edges of the columns, for a table at x that's w wide
struct Columns {
    int search, time, api;
};

static Columns columns(int x, int w){
    return {.search = x, .time = x + w - API_W - TIME_W, .api = x + w - API_W};
}

// text's first line, cut to fit max_w pixels in the current font, ending
// in "…" if anything was left out. Measures one character at a time, so a
// 5000-character search costs no more than the part that fits.
static std::string fit_line(std::string_view text, int max_w){
    static constexpr std::string_view ELLIPSIS = "…";
    const double ellipsis_w = fl_width(ELLIPSIS.data(), static_cast<int>(ELLIPSIS.size()));
    const size_t line_end = std::min(text.find('\n'), text.size());

    double width = 0;
    // End of the part that still leaves room for "…" after it
    size_t fits_with_ellipsis = 0;
    size_t i = 0;
    while (i < line_end){
        // At least 1, so a broken byte still moves the loop forward
        const size_t len = std::min<size_t>(std::max(1, fl_utf8len1(text[i])), line_end - i);
        width += fl_width(text.data() + i, static_cast<int>(len));
        if (width > max_w) break;
        i += len;
        if (width + ellipsis_w <= max_w) fits_with_ellipsis = i;
    }
    // All of it fit, and it's only one line
    if (i == text.size()) return std::string(text);
    return std::string(text.substr(0, fits_with_ellipsis)) + std::string(ELLIPSIS);
}

// Shorter the more recent, to fit the Time/Date column:
//   today:          14:52
//   the last week:  Mon 14:52
//   this year:      Oct 5
//   before that:    2025-10-05
static std::string short_time(std::int64_t time, const std::tm& now){
    const std::tm t = local_time(time);
    const auto days_ago = (day_of(now) - day_of(t)).count();
    if (days_ago == 0) return format_time(t, "%H:%M");
    if (days_ago > 0 && days_ago < 7) return format_time(t, "%a %H:%M");
    // Not %e, which pads the day with a space
    if (t.tm_year == now.tm_year) return format_time(t, "%b ") + std::to_string(t.tm_mday);
    return format_time(t, "%Y-%m-%d");
}

// A rounded square in the API's color, with its first letter
static void draw_badge(Api api, int x, int y){
    const auto i = static_cast<size_t>(api);
    fl_draw_box(FL_RFLAT_BOX, x, y, BADGE, BADGE, BADGE_COLORS[i]);
    fl_font(FL_HELVETICA_BOLD, 11);
    fl_color(FL_WHITE);
    fl_draw(BADGE_LETTERS[i], x, y, BADGE, BADGE, FL_ALIGN_CENTER);
}

// The rows: one per entry, at least as tall as the scroll it's in, so the
// "No searches yet" note can sit in its middle. Only draws the rows in
// view, so a long history costs no more than a short one.
class HistoryRows : public Fl_Widget {
public:
    HistoryRows(int x, int y, int w, int h) : Fl_Widget(x, y, w, h){
        // Draws its own background, since it can be redrawn without its
        // parent
        box(FL_FLAT_BOX);
        color(FL_BACKGROUND2_COLOR);
    }

    std::vector<const HistoryEntry*> entries;
    // Indexes into entries, -1 for none
    int selected = -1;
    int hovered = -1;
    // The selected one's query and API, to find it again among new
    // entries: the old ones may already be gone
    std::optional<std::pair<std::string, Api>> selected_key;
    std::function<void()> select_listener;

    // Row at window y, or -1 if there's none there
    int row_at(int y) const {
        if (y < this->y()) return -1;
        const int row = (y - this->y()) / ROW_H;
        return row < static_cast<int>(entries.size()) ? row : -1;
    }

    void select(int row){
        if (row == selected) return;
        selected = row;
        selected_key.emplace(entries[row]->query, entries[row]->api);
        redraw();
        if (select_listener) select_listener();
    }

    int handle(int event) override {
        switch (event){
        case FL_ENTER:
        case FL_MOVE:
            set_hovered(row_at(Fl::event_y()));
            return 1;    // 1 on FL_ENTER, or there are no FL_MOVEs
        case FL_LEAVE:
            set_hovered(-1);
            return 1;
        case FL_PUSH:
            if (Fl::event_button() != FL_LEFT_MOUSE) break;
            if (const int row = row_at(Fl::event_y()); row >= 0) select(row);
            return 1;
        }
        return Fl_Widget::handle(event);
    }

    void draw() override {
        draw_box();
        if (entries.empty()){
            fl_font(FL_HELVETICA_ITALIC, FL_NORMAL_SIZE);
            fl_color(DIM);
            fl_draw("No searches yet", x(), y(), w(), h(), FL_ALIGN_CENTER);
            return;
        }

        // The part on screen. The rest is clipped away by the scroll.
        int clip_x, clip_y, clip_w, clip_h;
        fl_clip_box(x(), y(), w(), h(), clip_x, clip_y, clip_w, clip_h);
        const int first = std::max(0, (clip_y - y()) / ROW_H);
        const int last = std::min(static_cast<int>(entries.size()), (clip_y + clip_h - y()) / ROW_H + 1);

        const std::tm now = local_time(unix_now());
        for (int i = first; i < last; ++i) draw_row(i, now);
    }

private:
    void set_hovered(int row){
        if (row == hovered) return;
        hovered = row;
        redraw();
    }

    void draw_row(int i, const std::tm& now) const {
        const HistoryEntry& entry = *entries[i];
        const int top = y() + i * ROW_H;
        // Computed here, not as constants: the colors can still change
        // (e.g. with -bg) before the first window is created
        if (i == selected)
            fl_draw_box(FL_FLAT_BOX, x(), top, w(), ROW_H, fl_color_average(ACCENT, FL_BACKGROUND2_COLOR, 0.2f));
        else if (i == hovered)
            fl_draw_box(FL_FLAT_BOX, x(), top, w(), ROW_H, fl_color_average(FL_BLACK, FL_BACKGROUND2_COLOR, 0.06f));

        const Columns col = columns(x(), w());
        draw_badge(entry.api, col.search + PAD, top + (ROW_H - BADGE) / 2);

        fl_font(FL_HELVETICA, FL_NORMAL_SIZE);
        const int baseline = top + (ROW_H + fl_height()) / 2 - fl_descent();
        const int text_x = col.search + PAD + BADGE + BADGE_GAP;
        fl_color(FL_FOREGROUND_COLOR);
        // Not fl_draw()'s boxed form, which would take '@' for a symbol
        fl_draw(fit_line(entry.query, col.time - PAD - text_x).c_str(), text_x, baseline);
        fl_color(DIM);
        fl_draw(short_time(entry.time, now).c_str(), col.time + PAD, baseline);
        fl_draw(API_NAMES[static_cast<size_t>(entry.api)], col.api + PAD, baseline);
    }
};

HistoryTable::HistoryTable(int x, int y, int w, int h) : Fl_Group(x, y, w, h){
    // The border goes around both the header and the rows
    box(FL_DOWN_BOX);
    color(FL_BACKGROUND2_COLOR);

    const int inner_x = x + Fl::box_dx(box()), inner_y = y + Fl::box_dy(box());
    const int inner_w = w - Fl::box_dw(box()), inner_h = h - Fl::box_dh(box());
    scroll = new SmoothScroll(inner_x, inner_y + HEADER_H, inner_w, inner_h - HEADER_H);
    // No border of its own: it's inside this one
    scroll->box(FL_FLAT_BOX);
    scroll->color(FL_BACKGROUND2_COLOR);
    // Stops before the scrollbar
    rows = new HistoryRows(inner_x, inner_y + HEADER_H, scroll->content_w(), scroll->h());
    scroll->end();
    end();
}

void HistoryTable::entries(std::vector<const HistoryEntry*> entries){
    int selected = -1;
    if (rows->selected_key){
        const auto& [query, api] = *rows->selected_key;
        const auto found = std::ranges::find_if(entries, [&](const HistoryEntry* e){ return e->query == query && e->api == api; });
        if (found != entries.end()) selected = static_cast<int>(found - entries.begin());
    }
    const bool lost = rows->selected >= 0 && selected < 0;

    rows->entries = std::move(entries);
    rows->selected = selected;
    if (selected < 0) rows->selected_key.reset();
    // Found again on the next mouse move: it may be another row now
    rows->hovered = -1;
    rows->size(rows->w(), std::max(static_cast<int>(rows->entries.size()) * ROW_H, scroll->h()));
    // Its scrollbar depends on the new height
    scroll->redraw();

    if (lost && rows->select_listener) rows->select_listener();
}

const HistoryEntry* HistoryTable::selected() const {
    return rows->selected >= 0 ? rows->entries[rows->selected] : nullptr;
}

void HistoryTable::on_select(std::function<void()> listener){
    rows->select_listener = std::move(listener);
}

void HistoryTable::draw(){
    Fl_Group::draw();

    // The header, over the strip above the scroll
    const int left = x() + Fl::box_dx(box()), top = y() + Fl::box_dy(box());
    const int width = w() - Fl::box_dw(box());
    fl_draw_box(FL_FLAT_BOX, left, top, width, HEADER_H,
                fl_color_average(FL_BACKGROUND_COLOR, FL_BACKGROUND2_COLOR, 0.5f));
    fl_color(fl_color_average(FL_BLACK, FL_BACKGROUND2_COLOR, 0.15f));
    fl_xyline(left, top + HEADER_H - 1, left + width - 1);

    // Lined up with the rows, which stop before the scrollbar
    const Columns col = columns(rows->x(), rows->w());
    for (const int divider : {col.time, col.api}) fl_yxline(divider, top + 6, top + HEADER_H - 7);

    fl_font(FL_HELVETICA, 12);
    fl_color(DIM);
    const int baseline = top + (HEADER_H + fl_height()) / 2 - fl_descent();
    fl_draw("Search", col.search + PAD, baseline);
    fl_draw("Time/Date", col.time + PAD, baseline);
    fl_draw("API", col.api + PAD, baseline);
}
