#include "ui/history_window.h"

#include <format>
#include <string>
#include <vector>

// FLTK
#include <FL/Fl.H>

#include "core/api.h"
#include "core/history.h"
#include "core/local_time.h"
#include "ui/history_store.h"
#include "ui/history_table.h"
#include "ui/smooth_scroll.h"
#include "ui/theme.h"
#include "ui/wrapped_text.h"

// Space around the table and the details pane
static constexpr int MARGIN = 10;
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

HistoryWindow::HistoryWindow(HistoryStore& history) : history(history){
    // win is the current group here, like in MainWindow

    const int inner_w = win.w() - 2 * MARGIN;
    table = new HistoryTable(MARGIN, MARGIN, inner_w, win.h() - 3 * MARGIN - DETAILS_H);
    table->on_select([this]{ show_details(); });

    details = new SmoothScroll(MARGIN, table->y() + table->h() + MARGIN, inner_w, DETAILS_H);
    // Inside the box's border, and stops before the scrollbar
    details_text = new WrappedText(details->x() + Fl::box_dx(details->box()),
                                   details->y() + Fl::box_dy(details->box()), details->content_w());
    details->end();

    // No resizable() on purpose: FLTK then keeps the window at a fixed size.
    win.end();

    history_listener = history.add_listener([this]{ refresh(); });
    refresh();
}

HistoryWindow::~HistoryWindow(){
    history.remove_listener(history_listener);
}

void HistoryWindow::show(){
    win.show();
}

void HistoryWindow::refresh(){
    std::vector<const HistoryEntry*> entries;
    for (const HistoryEntry& entry : history.entries()) entries.push_back(&entry);
    table->entries(std::move(entries));
    // The selected search may have changed too, e.g. been made again
    show_details();
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
