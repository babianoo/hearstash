#pragma once

#include <array>
#include <optional>

// FLTK
#include <FL/Fl_Double_Window.H>

#include "core/time_ranges.h"

// Only pointers and references to these are stored here, so the full
// headers are included in history_window.cpp instead.
class CategoryButton;
class FilterBar;
class HistoryStore;
class HistoryTable;
class SmoothScroll;
class WrappedText;
class Fl_Group;
class Fl_Widget;

// The window the clock button opens, laid out like a browser's history:
// on the left a sidebar of time ranges (Today, Yesterday...), each only
// if it has searches. On the right a box that filters the searches by
// their text, the searches in a table, newest first, and under it the
// selected one in full, with its DeepL translation if there's one kept.
// It follows the history as it changes, open or not.
class HistoryWindow {
public:
    // history has to outlive the window
    explicit HistoryWindow(HistoryStore& history);
    // Stops listening to history
    ~HistoryWindow();

    // One history window per app: copying makes no sense
    HistoryWindow(const HistoryWindow&) = delete;
    HistoryWindow& operator=(const HistoryWindow&) = delete;

    // Opens the window, or raises it if it's already open
    void show();

private:
    HistoryStore& history;
    // From history.add_listener
    int history_listener = -1;

    // Owned by HistoryWindow as a member, so no new/delete anywhere.
    // Closing the window only hides it.
    Fl_Double_Window win{860, 560, "History"};

    // Created with new in the constructor and added to win, which deletes
    // them when it dies. The buttons are the sidebar's children.
    Fl_Group* sidebar = nullptr;
    // Shows every range
    CategoryButton* all_btn = nullptr;
    // Indexed by TimeRange
    std::array<CategoryButton*, TIME_RANGE_COUNT> range_btns{};
    FilterBar* filter = nullptr;
    HistoryTable* table = nullptr;
    // details_text is details' only child
    SmoothScroll* details = nullptr;
    WrappedText* details_text = nullptr;

    // The range whose searches are shown, or all of them if none
    std::optional<TimeRange> shown_range;

    // Static because FLTK callbacks are plain functions; the HistoryWindow
    // comes in through data.
    static void on_range(Fl_Widget* w, void* data);
    static void on_filter(Fl_Widget* w, void* data);

    // Places everything right of the sidebar, or across the whole window
    // while it's hidden
    void layout();
    // Shows the history as it is now
    void refresh();
    // Shows the ranges that have searches, as of now
    void refresh_sidebar(const std::array<bool, TIME_RANGE_COUNT>& has_searches, const std::tm& now);
    // Shows the selected search in the details pane
    void show_details();
};
