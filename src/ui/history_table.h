#pragma once

#include <functional>
#include <vector>

// FLTK
#include <FL/Fl_Group.H>

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
// Clicking a row selects it.
class HistoryTable : public Fl_Group {
public:
    HistoryTable(int x, int y, int w, int h);

    // Shows these entries, top to bottom. They must stay valid until the
    // next call. The selected search stays selected if it's among them.
    void entries(std::vector<const HistoryEntry*> entries);

    // nullptr when none is
    const HistoryEntry* selected() const;

    // listener is called after the selection changes: by a click, or
    // because the selected search is no longer shown
    void on_select(std::function<void()> listener);

protected:
    void draw() override;

private:
    // Created with new in the constructor and added to this group, which
    // deletes them when it dies. rows is scroll's only child.
    SmoothScroll* scroll = nullptr;
    HistoryRows* rows = nullptr;
};
