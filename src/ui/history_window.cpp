#include "ui/history_window.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <string_view>
#include <vector>

// FLTK
#include <FL/Fl.H>
#include <FL/Fl_Group.H>

#include "core/api.h"
#include "core/history.h"
#include "core/local_time.h"
#include "ui/category_button.h"
#include "ui/filter_bar.h"
#include "ui/history_store.h"
#include "ui/history_table.h"
#include "ui/smooth_scroll.h"
#include "ui/theme.h"
#include "ui/wrapped_text.h"

// Space around the filter box, the table and the details pane
static constexpr int MARGIN = 10;
static constexpr int SIDEBAR_W = 170;
static constexpr int CATEGORY_H = 32;
// Left of the sidebar's text, and the ranges indented under "History"
static constexpr int ALL_INDENT = 20, RANGE_INDENT = 36;
// The filter box is no wider than this, centered over the table
static constexpr int FILTER_W = 420;
static constexpr int DETAILS_H = 130;

// Looks of the details pane's parts
static const TextStyle LABEL{FL_HELVETICA_BOLD, 12, DIM};
static const TextStyle INFO{FL_HELVETICA, 12, DIM};
static const TextStyle NOTE{FL_HELVETICA_ITALIC, FL_NORMAL_SIZE, DIM};

// In full, e.g. "Monday, 26 October 2026, 14:52"
static std::string long_time(std::int64_t time){
    const std::tm t = local_time(time);
    // Not %e, which pads the day with a space
    return std::format("{}{}{}", format_time(t, "%A, "), t.tm_mday, format_time(t, " %B %Y, %H:%M"));
}

// Whether needle is in text, ignoring case. Only for A-Z: Japanese has
// no case.
static bool contains(std::string_view text, std::string_view needle){
    const auto same = [](char a, char b){
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };
    return !std::ranges::search(text, needle, same).empty();
}

// Whether the filter box's text is in entry's search or translation. All
// are when it's empty.
static bool matches(const HistoryEntry& entry, std::string_view filter){
    return filter.empty() || contains(entry.query, filter)
        || (entry.translation && contains(*entry.translation, filter));
}

HistoryWindow::HistoryWindow(HistoryStore& history) : history(history){
    // win is the current group here, like in MainWindow

    // Its own group so the radio buttons only deselect each other. The
    // buttons are placed by refresh_sidebar(), which only shows the ranges
    // that have searches.
    sidebar = new Fl_Group(0, 0, SIDEBAR_W, win.h());
    sidebar->box(FL_FLAT_BOX);
    sidebar->color(sidebar_color());
    all_btn = new CategoryButton(0, 0, SIDEBAR_W, CATEGORY_H, "History", ALL_INDENT);
    all_btn->callback(on_range, this);
    for (CategoryButton*& btn : range_btns){
        btn = new CategoryButton(0, 0, SIDEBAR_W, CATEGORY_H, nullptr, RANGE_INDENT);
        btn->callback(on_range, this);
    }
    sidebar->end();

    // These are placed by layout()
    filter = new FilterBar(0, 0, FILTER_W, "Search history");
    filter->callback(on_filter, this);

    table = new HistoryTable(0, 0, win.w(), win.h());
    table->on_select([this]{ show_details(); });

    details = new SmoothScroll(0, 0, win.w(), DETAILS_H);
    // Inside the box's border
    details_text = new WrappedText(Fl::box_dx(details->box()), Fl::box_dy(details->box()), details->content_w());
    details->end();

    // No resizable() on purpose: FLTK then keeps the window at a fixed size.
    win.end();
    layout();

    history_listener = history.add_listener([this]{ refresh(); });
    refresh();
}

HistoryWindow::~HistoryWindow(){
    history.remove_listener(history_listener);
}

void HistoryWindow::show(){
    // A new day may have begun since it was last shown, which moves
    // searches to other ranges
    refresh();
    win.show();
}

void HistoryWindow::layout(){
    const int left = (sidebar->visible() ? SIDEBAR_W : 0) + MARGIN;
    const int width = win.w() - left - MARGIN;

    const int filter_w = std::min(FILTER_W, width);
    filter->resize(left + (width - filter_w) / 2, MARGIN, filter_w, PILL_H);

    const int table_y = filter->y() + filter->h() + MARGIN;
    const int details_y = win.h() - MARGIN - DETAILS_H;
    table->resize(left, table_y, width, details_y - MARGIN - table_y);

    details->resize(left, details_y, width, DETAILS_H);
    // The scroll moves its child along, but doesn't resize it. Stops
    // before the scrollbar. Its height is the text's, which it sets itself.
    details_text->resize(details_text->x(), details_text->y(), details->content_w(), details_text->h());

    win.redraw();
}

void HistoryWindow::refresh(){
    const std::tm now = local_time(unix_now());

    std::array<bool, TIME_RANGE_COUNT> has_searches{};
    for (const HistoryEntry& entry : history.entries())
        has_searches[static_cast<size_t>(range_of(entry.time, now))] = true;
    // E.g. its searches were deleted, or have moved on to the next range
    // with the new day. All of them are shown instead.
    if (shown_range && !has_searches[static_cast<size_t>(*shown_range)]) shown_range.reset();
    refresh_sidebar(has_searches, now);

    const std::string filter_text = filter->text();
    std::vector<const HistoryEntry*> entries;
    for (const HistoryEntry& entry : history.entries()){
        if (shown_range && range_of(entry.time, now) != *shown_range) continue;
        if (matches(entry, filter_text)) entries.push_back(&entry);
    }
    // A range is only in the sidebar while it has searches, so with some
    // in the history, it's the filter that left none
    table->entries(std::move(entries), history.entries().empty() ? "No searches yet" : "No searches match");
    // The selected search may have changed too, e.g. been made again
    show_details();
}

void HistoryWindow::refresh_sidebar(const std::array<bool, TIME_RANGE_COUNT>& has_searches, const std::tm& now){
    // Lines up with the settings window's first category
    int y = 2 * MARGIN;
    all_btn->position(0, y);
    y += CATEGORY_H;
    for (size_t i = 0; i < TIME_RANGE_COUNT; ++i){
        CategoryButton* btn = range_btns[i];
        if (!has_searches[i]){
            btn->hide();
            continue;
        }
        // Month names change as time goes on
        btn->copy_label(range_name(static_cast<TimeRange>(i), now).c_str());
        btn->position(0, y);
        btn->show();
        y += CATEGORY_H;
    }

    (shown_range ? range_btns[static_cast<size_t>(*shown_range)] : all_btn)->setonly();
    sidebar->redraw();
}

void HistoryWindow::on_range(Fl_Widget* w, void* data){
    HistoryWindow* self = static_cast<HistoryWindow*>(data);
    const auto found = std::ranges::find(self->range_btns, w);
    if (found == self->range_btns.end()) self->shown_range.reset();
    else self->shown_range = static_cast<TimeRange>(found - self->range_btns.begin());

    self->table->scroll_to_top();
    self->refresh();
}

void HistoryWindow::on_filter(Fl_Widget*, void* data){
    HistoryWindow* self = static_cast<HistoryWindow*>(data);
    self->table->scroll_to_top();
    self->refresh();
}

void HistoryWindow::show_details(){
    std::vector<TextSpan> spans;
    if (const HistoryEntry* entry = table->selected()){
        spans.push_back({"Search\n", LABEL});
        spans.push_back({entry->query + '\n'});
        if (entry->translation){
            spans.push_back({"\nTranslation\n", LABEL});
            spans.push_back({*entry->translation + '\n'});
        }
        spans.push_back({std::format("\n{} · {}", API_NAMES[static_cast<size_t>(entry->api)], long_time(entry->time)), INFO});
    } else {
        spans.push_back({"Select a search to see all of it here.", NOTE});
    }

    details->scroll_to_top();
    details_text->text(spans);
    // Its scrollbar depends on the new text's height
    details->redraw();
}
