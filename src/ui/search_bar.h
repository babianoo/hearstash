#pragma once

// FLTK
#include <FL/Fl_Group.H>

// Only pointers to these are stored here, so the full headers are
// included in search_bar.cpp instead.
class Fl_Input;
class Fl_Button;
class Fl_Widget;

// Same APIs as japanese-helper. Order matches the API menu.
enum class Api { Jisho, DeepL, MyMemory };

// Google-style pill search bar, always PILL_H tall: "+" API menu in the
// left end, text input in the middle, Search button in the right end.
//
// Clicking Search or pressing Enter calls the bar's own callback(), so a
// window sets it like for any other widget.
class SearchBar : public Fl_Group {
public:
    SearchBar(int x, int y, int w);

    const char* text() const;
    Api api() const { return current_api; }

private:
    // Created with new in the constructor and added to this group, which
    // deletes them when it dies. SearchBar only keeps a handle to them.
    Fl_Input* input = nullptr;
    Fl_Button* search_btn = nullptr;
    Fl_Button* api_btn = nullptr;

    Api current_api = Api::Jisho;

    // Static because FLTK callbacks are plain functions; the SearchBar
    // comes in through data.
    static void on_api_btn(Fl_Widget* w, void* data);
    static void on_submit(Fl_Widget* w, void* data);
};
