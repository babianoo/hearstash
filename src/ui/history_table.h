#pragma once

#include <functional>
#include <string>
#include <vector>

// FLTK
#include <FL/Fl_Group.H>

#include "core/api.h"
#include "core/history.h"

// Only pointers to these are stored here, so the full headers are
// included in history_table.cpp instead.
class SmoothScroll;
// The rows under the header. Defined in history_table.cpp, since only
// HistoryTable uses it.
class HistoryRows;

// Searches as a table, like a browser's history: a header row with the
// Search, Time/Date and API columns, then one row per search, scrolling
// smoothly under it. A search's first line is cut to fit its column, with
// "…" if anything was left out.
//
// Clicking a row selects it, and so do the arrow keys, Page Up, Page Down,
// Home and End once the table has focus. Double-clicking a row or pressing
// Enter makes its search again, Delete deletes it, and Ctrl+C copies it.
// Right-clicking a row opens a menu with Search Again, Copy and Delete.
class HistoryTable : public Fl_Group {
public:
    HistoryTable(int x, int y, int w, int h);

    // Shows these entries, top to bottom. They must stay valid until the
    // next call. The selected search stays selected if it's among them.
    // Without any, empty_note is shown in their place; it isn't copied.
    void entries(std::vector<const HistoryEntry*> entries, const char* empty_note);

    // nullptr when none is
    const HistoryEntry* selected() const;
    // Its index in the entries, -1 when none is
    int selected_row() const;
    // Selects row, scrolled into view, e.g. the one after a deleted search.
    // Clamped to the rows there are; does nothing without any.
    void select(int row);

    // listener is called after the selection changes: by a click, or
    // because the selected search is no longer shown
    void on_select(std::function<void()> listener);
    // listener is called with the selected search to make again, or to
    // delete. They're copies: either changes the history, and so the
    // entries.
    void on_activate(std::function<void(const std::string& query, Api api)> listener);
    void on_delete(std::function<void(const std::string& query, Api api)> listener);

    // A key pressed elsewhere, e.g. in a box that filters the table, to
    // act on it as if the table had focus. Returns whether it took key.
    bool handle_key(int key);

    // Jumps to the first row, e.g. when other searches are shown
    void scroll_to_top();

    // Keeps the header its height and the rows theirs, as wide as the
    // table. Fl_Group's own resize() would stretch both.
    void resize(int x, int y, int w, int h) override;

protected:
    void draw() override;

private:
    // Created with new in the constructor and added to this group, which
    // deletes them when it dies. rows is scroll's only child.
    SmoothScroll* scroll = nullptr;
    HistoryRows* rows = nullptr;

    // Places the scroll under the header, inside the border
    void place_scroll();
    // Makes the rows as wide as the scroll's content, and tall enough for
    // all of them, or the scroll if that's taller
    void fit_rows();
};
