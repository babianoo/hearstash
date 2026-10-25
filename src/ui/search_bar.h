#pragma once

#include <functional>
#include <string>

// FLTK
#include <FL/Fl_Group.H>

// Only pointers to these are stored here, so the full headers are
// included in search_bar.cpp instead.
class Fl_Box;
class Fl_Button;
class Fl_Widget;
// The text box. Defined in search_bar.cpp, since only SearchBar uses it.
class SearchInput;

// Same APIs as japanese-helper. Order matches the API menu.
enum class Api { Jisho, DeepL, MyMemory };

// Google-style pill search bar: "+" API menu in the left end, text box in
// the middle, Search button in the right end. It's PILL_H tall while the
// text fits on one line. Longer text wraps, and the bar grows down with
// it, up to MAX_LINES lines; past that the text box scrolls.
//
// Clicking Search or pressing Enter calls the bar's own callback(), so a
// window sets it like for any other widget. Shift+Enter adds a new line.
class SearchBar : public Fl_Group {
public:
    static constexpr int MAX_LINES = 5;

    SearchBar(int x, int y, int w);

    std::string text() const;
    Api api() const { return current_api; }

    // Shrinks the bar to the text's first line, e.g. once a search starts,
    // so the results get the space. Clicking or editing the text shows all
    // of it again.
    void collapse();

    // listener is called after the bar's height changes, so the window can
    // move what's under it
    void on_height_change(std::function<void()> listener){ height_listener = std::move(listener); }

    // Keeps the buttons on the top line and gives the text box the rest.
    // Fl_Group's own resize() would stretch the buttons along with the bar.
    void resize(int x, int y, int w, int h) override;
    int handle(int event) override;

protected:
    void draw() override;

private:
    // Created with new in the constructor and added to this group, which
    // deletes them when it dies. SearchBar only keeps a handle to them.
    SearchInput* input = nullptr;
    Fl_Button* search_btn = nullptr;
    Fl_Button* api_btn = nullptr;
    Fl_Box* divider = nullptr;

    Api current_api = Api::Jisho;

    // Height of a line of text. 0 until the bar is shown, since measuring
    // the font needs the display.
    int line_h = 0;
    // How many lines of text the bar is tall enough for
    int shown_lines = 1;
    bool collapsed = false;

    std::function<void()> height_listener;

    // Space above and below the text box, so its first line is centered
    // on the buttons
    int text_pad() const;
    int height_for(int lines) const;
    void place_children();
    // Grows or shrinks the bar to fit the text, or one line when collapsed
    void fit_text();
    // The pill stretched to the bar's height
    void draw_pill();

    // Static because FLTK callbacks are plain functions; the SearchBar
    // comes in through data.
    static void on_api_btn(Fl_Widget* w, void* data);
    static void on_submit(Fl_Widget* w, void* data);
    // Fl_Text_Buffer's modify callback
    static void on_text_modified(int pos, int inserted, int deleted, int restyled,
                                 const char* deleted_text, void* data);
};
