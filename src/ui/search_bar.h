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

// What the "+" button shows about recording speech, which the window sets
enum class RecordState { Idle, Recording, Transcribing };

// Google-style pill search bar: "+" menu in the left end, text box in the
// middle, Search button in the right end. The menu has Record Input and
// the APIs. While recording, the "+" button is a red mic that stops it. It's PILL_H tall while the
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

    // listener is called with true when the mouse moves onto the text box,
    // and false when it leaves, e.g. to hide another scrollbar meanwhile
    void on_input_hover(std::function<void(bool hovered)> listener);

    // listener is called when the user picks Record Input, or clicks the
    // "+" button while Recording to stop. The window does it, then sets
    // the state to match.
    void on_record(std::function<void()> listener){ record_listener = std::move(listener); }
    void record_state(RecordState state);
    RecordState record_state() const { return current_record_state; }
    // Puts text in the text box, replacing what's there, with the cursor
    // at its end, e.g. what was said
    void set_text(const std::string& text);

    // Keeps the API button its size, centered, and stretches the text box
    // and Search button to the bar's height. Fl_Group's own resize() would
    // stretch the API button too.
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
    RecordState current_record_state = RecordState::Idle;
    std::function<void()> record_listener;

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
    // Makes the "+" button show current_record_state
    void show_record_state();

    // Static because FLTK callbacks are plain functions; the SearchBar
    // comes in through data.
    static void on_api_btn(Fl_Widget* w, void* data);
    static void on_submit(Fl_Widget* w, void* data);
    // Fl_Text_Buffer's modify callback
    static void on_text_modified(int pos, int inserted, int deleted, int restyled,
                                 const char* deleted_text, void* data);
};
