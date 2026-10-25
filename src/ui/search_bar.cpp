#include "ui/search_bar.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <format>
#include <functional>
#include <iterator>
#include <string>
#include <utility>

// FLTK
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Multi_Label.H>
#include <FL/Fl_SVG_Image.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Editor.H>

#include "ui/clipboard.h"
#include "ui/icons.h"
#include "ui/theme.h"

// Indexed by Api
static constexpr const char* API_NAMES[] = {"Jisho", "DeepL", "MyMemory"};

// The "+" button's fill while recording
static const Fl_Color RECORDING_RED = fl_rgb_color(220, 53, 53);
// And while transcribing
static const Fl_Color TRANSCRIBING_GREY = fl_rgb_color(222, 222, 222);

// The mic icon, 16 pixels: dark grey like the other icons, for the menu,
// or white for the "+" button on RECORDING_RED. Made once and kept for the
// whole program; buttons and menus only borrow them.
static Fl_SVG_Image* mic_icon(bool white){
    static Fl_SVG_Image* dark = []{
        auto* icon = new Fl_SVG_Image(nullptr, ICON_MIC);
        icon->scale(16, 16);
        return icon;
    }();
    static Fl_SVG_Image* light = []{
        // Static too, in case the image keeps pointing into it
        static std::string svg = ICON_MIC;
        for (size_t at; (at = svg.find("#444")) != std::string::npos;) svg.replace(at, 4, "#fff");
        auto* icon = new Fl_SVG_Image(nullptr, svg.c_str());
        icon->scale(16, 16);
        return icon;
    }();
    return white ? light : dark;
}

// Space Fl_Text_Display leaves between its edges and the text: 3 pixels
// on the left and right, 1 on the top and bottom
static constexpr int TEXT_MARGIN_X = 3;
static constexpr int TEXT_MARGIN_Y = 2;    // Both together

// Scroll handle, like SmoothScroll's: a strip along the right edge with a
// rounded grey handle
static constexpr int HANDLE_W = 8;
static constexpr int MIN_HANDLE_H = 24;
static const Fl_Color HANDLE_COLOR = fl_rgb_color(160, 160, 160);

// Fl_Text_Editor set up as the bar's text box. Text wraps at the width,
// anywhere when there are no spaces (like Japanese). Enter calls the
// callback; Shift+Enter adds a new line. Copy, cut and paste go through
// ui/clipboard.h, like in ClipboardInput.
//
// Fl_Input can't be used: it draws a line through a 1024-byte buffer and
// puts the rest on a second line, so a long one-line paste breaks.
//
// It never shows FLTK's scrollbars, which have arrow buttons. The wheel
// scrolls it instead, and draw() adds a thin handle when it can scroll and
// the mouse is over it. Only then, so it doesn't look like a second
// scrollbar for the results under it.
class SearchInput : public Fl_Text_Editor {
public:
    SearchInput(int x, int y, int w, int h);
    ~SearchInput() override;

    // Lines the text takes, wrapped. Stops counting at max_lines, which is
    // quicker for a long text.
    int lines(int max_lines = INT_MAX) const;

    // Called with true when the mouse moves onto the text box, and false
    // when it leaves
    std::function<void(bool hovered)> hover_listener;

    void resize(int x, int y, int w, int h) override;
    int handle(int event) override;

protected:
    void draw() override;

private:
    // Whether the mouse is over the text box, which shows the handle
    bool hovered = false;

    // Wraps at a fixed width, before the handle's strip, instead of at the
    // edge (WRAP_AT_BOUNDS): then lines() is right straight away, rather
    // than after the next draw(), and doesn't change with the handle
    void wrap_to_width();
    void copy_selection();
};

SearchInput::SearchInput(int x, int y, int w, int h) : Fl_Text_Editor(x, y, w, h){
    // Deleted in the destructor; Fl_Text_Display doesn't own its buffer
    buffer(new Fl_Text_Buffer);
    // Same font as Fl_Input, and its thin cursor
    textfont(FL_HELVETICA);
    textsize(FL_NORMAL_SIZE);
    cursor_style(SIMPLE_CURSOR);
    // Neither left/right nor top/bottom: no scrollbars at all
    scrollbar_align(FL_ALIGN_CENTER);
    // Tab moves to the next widget instead of typing a tab
    tab_nav(1);
    // The callback only runs on Enter, from handle(). Not on losing focus,
    // which Fl_Text_Editor would do by default.
    when(FL_WHEN_NEVER);
    wrap_to_width();
}

SearchInput::~SearchInput(){
    // Detached first: Fl_Text_Display's destructor still uses its buffer
    Fl_Text_Buffer* buf = buffer();
    buffer(nullptr);
    delete buf;
}

int SearchInput::lines(int max_lines) const {
    // Not count_lines(): it counts a last line without a newline as
    // buf->next_char(count) lines, which treats the count as a position in
    // the text. That's 1 too many, or more in UTF-8 text like Japanese.
    int end, breaks, last_start, last_end;
    wrapped_line_counter(buffer(), 0, buffer()->length(), max_lines - 1, true, 0,
                         &end, &breaks, &last_start, &last_end, false);
    return breaks + 1;
}

void SearchInput::resize(int x, int y, int w, int h){
    const bool width_changed = w != this->w();
    Fl_Text_Editor::resize(x, y, w, h);
    if (width_changed) wrap_to_width();
}

void SearchInput::wrap_to_width(){
    // Also a small gap before the handle's strip
    const int width = w() - Fl::box_dw(box()) - TEXT_MARGIN_X - HANDLE_W - 2;
    wrap_mode(WRAP_AT_PIXEL, std::max(1, width));
}

int SearchInput::handle(int event){
    if (event == FL_ENTER || event == FL_LEAVE){
        hovered = event == FL_ENTER;
        redraw();    // Shows or hides the handle
        if (hover_listener) hover_listener(hovered);
        Fl_Text_Editor::handle(event);    // Sets the mouse cursor
        return 1;    // Needed to get FL_LEAVE too
    }

    if (event == FL_KEYBOARD){
        const int mods = Fl::event_state() & (FL_META | FL_CTRL | FL_ALT);
        const bool shift = Fl::event_state(FL_SHIFT);
        const int key = Fl::event_key();
        // Shift+Enter goes on to Fl_Text_Editor, which adds a new line.
        // Not while an input method is composing, which Enter confirms.
        if ((key == FL_Enter || key == FL_KP_Enter) && !shift && Fl::compose_state == 0){
            do_callback(FL_REASON_ENTER_KEY);
            return 1;
        }
        // Same combinations as Fl_Text_Editor
        if ((key == 'c' && mods == FL_COMMAND) || (key == FL_Insert && mods == FL_CTRL)){
            copy_selection();
            return 1;
        }
        if (key == 'x' && mods == FL_COMMAND){
            copy_selection();
            if (buffer()->selected()) kf_delete(0, this);
            return 1;
        }
        if ((key == 'v' && mods == FL_COMMAND) || (key == FL_Insert && mods == 0 && shift)){
            clipboard_paste(*this);
            return 1;
        }
    }

    if (event == FL_PUSH && Fl::event_button() == FL_RIGHT_MOUSE){
        if (Fl::focus() != this){
            Fl::focus(this);
            handle(FL_FOCUS);
        }
        // Fl_Text_Display's menu, which first selects the word under the
        // mouse, but with the clipboard of ui/clipboard.h
        switch (handle_rmb(0)){
        case 1:
            copy_selection();
            if (buffer()->selected()) kf_delete(0, this);
            break;
        case 2:
            copy_selection();
            break;
        case 3:
            clipboard_paste(*this);
            break;
        }
        return 1;
    }

    // Fl_Text_Display only scrolls on the wheel when its scrollbar shows.
    // mTopLineNumHint is where the last scroll() is heading, so quick
    // turns of the wheel add up before the next draw() gets there.
    if (event == FL_MOUSEWHEEL && Fl::event_dy() != 0){
        const int last_top = lines() - mNVisibleLines + 1;
        if (last_top > 1){
            scroll(std::clamp(mTopLineNumHint + Fl::event_dy(), 1, last_top), 0);
            return 1;
        }
    }

    return Fl_Text_Editor::handle(event);
}

void SearchInput::copy_selection(){
    char* selected = buffer()->selection_text();
    if (*selected) clipboard_copy(selected);
    std::free(selected);
}

void SearchInput::draw(){
    Fl_Text_Editor::draw();

    // Keeping the cursor in view, Fl_Text_Display uses the line count that
    // lines() avoids, and scrolls past the end of the text. Scrolled back
    // and drawn again; the window's double buffering hides the first draw.
    const int total = lines();
    const int last_top = std::max(1, total - mNVisibleLines + 1);
    if (mTopLineNum > last_top){
        scroll(last_top, 0);
        Fl_Text_Editor::draw();
    }

    // The whole strip, which the text never reaches (see wrap_to_width())
    const int bar_x = x() + w() - Fl::box_dx(box()) - HANDLE_W;
    fl_color(color());
    fl_rectf(bar_x, y(), HANDLE_W, h());

    // None while there's just one line on screen, e.g. when collapsed
    if (!hovered || mNVisibleLines < 2 || total <= mNVisibleLines) return;

    const int track_y = y() + 2, track_h = h() - 4;
    const int handle_h = std::max(track_h * mNVisibleLines / total, MIN_HANDLE_H);
    const int scrolled = std::min(mTopLineNum - 1, total - mNVisibleLines);
    const int handle_y = track_y + (track_h - handle_h) * scrolled / (total - mNVisibleLines);
    fl_draw_box(FL_RFLAT_BOX, bar_x, handle_y, HANDLE_W, handle_h, HANDLE_COLOR);
}

// Draws boxtype b h tall the way it looks pill_h tall: its top half at the
// top, its bottom half at the bottom, and its middle row repeated in
// between. A round box drawn taller gets rounder instead, until it's a
// circle, which would cut into what's inside.
static void draw_stretched(Fl_Boxtype b, int x, int y, int w, int h, Fl_Color c, int pill_h){
    if (h <= pill_h){
        fl_draw_box(b, x, y, w, h, c);
        return;
    }
    const int top = pill_h / 2, bottom = pill_h - top;
    fl_push_clip(x, y, w, top);
    fl_draw_box(b, x, y, w, pill_h, c);
    fl_pop_clip();
    fl_push_clip(x, y + h - bottom, w, bottom);
    fl_draw_box(b, x, y + h - pill_h, w, pill_h, c);
    fl_pop_clip();
    for (int row = y + top; row < y + h - bottom; ++row){
        fl_push_clip(x, row, w, 1);
        fl_draw_box(b, x, row - (top - 1), w, pill_h, c);    // Its last top row
        fl_pop_clip();
    }
}

// Search button that's as tall as the bar, with the ends of a PILL_H - 6
// tall one. Never draws a focus ring: it can't take focus (see below), as
// FLTK's ring for a round box would be an oval when it's tall.
class SearchButton : public Fl_Button {
public:
    using Fl_Button::Fl_Button;

protected:
    void draw() override {
        // As in Fl_Button::draw()
        const Fl_Color col = value() ? selection_color() : color();
        const Fl_Boxtype b = value() ? (down_box() ? down_box() : fl_down(box())) : box();
        draw_stretched(b, x(), y(), w(), h(), col, PILL_H - 6);
        draw_label();
    }
};

SearchBar::SearchBar(int x, int y, int w) : Fl_Group(x, y, w, PILL_H){
    // Fl_Group's constructor calls begin(), so widgets created below are
    // added to this group. place_children() at the end gives them their
    // positions, which are window coordinates.
    //
    // FLTK doesn't handle overlapping widgets: when the text box redraws
    // (e.g. on focus) it would paint over a button placed on top of it. So
    // the pill is just the group's own box, drawn behind its children, and
    // the text box and button sit side by side inside it.
    box(FL_ROUND_UP_BOX);
    color(FL_BACKGROUND2_COLOR);    // Same white as the text box's fill

    // Created before the button so it's the widget that gets keyboard
    // focus when the window opens
    input = new SearchInput(x, y, w, PILL_H);
    input->box(FL_FLAT_BOX);
    input->callback(on_submit, this);
    input->buffer()->add_modify_callback(on_text_modified, this);

    search_btn = new SearchButton(x, y, w, PILL_H, "Search");
    search_btn->box(FL_ROUND_UP_BOX);
    search_btn->clear_visible_focus();    // Like the API button
    search_btn->callback(on_submit, this);

    // 1px line, 85% of the way from the text color to the input's white,
    // so it's barely visible.
    divider = new Fl_Box(x, y, w, PILL_H);
    divider->box(FL_FLAT_BOX);
    divider->color(fl_color_average(FL_BACKGROUND2_COLOR, FL_FOREGROUND_COLOR, 0.85f));

    // "+" that opens the menu (Record Input, API). No box, so the pill shows through; it
    // only draws a round grey circle while pressed. (A white FL_FLAT_BOX
    // would be square and cover the pill's curved edge.) The tooltip shows
    // the current API.
    api_btn = new Fl_Button(x, y, w, PILL_H, "+");
    api_btn->box(FL_NO_BOX);
    api_btn->down_box(FL_ROUND_DOWN_BOX);
    api_btn->tooltip(API_NAMES[static_cast<int>(current_api)]);
    api_btn->clear_visible_focus();      // No dotted focus ring after a click
    api_btn->callback(on_api_btn, this);

    end();
    place_children();
    show_record_state();
}

void SearchBar::record_state(RecordState state){
    current_record_state = state;
    show_record_state();
}

void SearchBar::show_record_state(){
    // At rest first, then changed to match
    api_btn->box(FL_NO_BOX);
    api_btn->down_box(FL_ROUND_DOWN_BOX);
    api_btn->label("+");
    api_btn->image(nullptr);
    api_btn->tooltip(API_NAMES[static_cast<int>(current_api)]);
    api_btn->activate();

    if (current_record_state == RecordState::Recording){
        // A red circle with a white mic. Pressed it stays a circle, a
        // little darker.
        api_btn->box(FL_OVAL_BOX);
        api_btn->down_box(FL_OVAL_BOX);
        api_btn->color(RECORDING_RED);
        api_btn->selection_color(fl_darker(RECORDING_RED));
        api_btn->label(nullptr);
        api_btn->image(mic_icon(true));
        api_btn->tooltip("Recording: click to stop");
    } else if (current_record_state == RecordState::Transcribing){
        // Greyed out until the text is in, on a light grey circle: a greyed
        // "+" alone looks much like the one at rest
        api_btn->box(FL_OVAL_BOX);
        api_btn->color(TRANSCRIBING_GREY);
        api_btn->deactivate();
        api_btn->tooltip("Transcribing...");
    }

    // The button has no box at rest, so the pill behind it has to be drawn
    // again to cover a red circle
    redraw();
}

void SearchBar::record_time(int seconds){
    if (current_record_state != RecordState::Recording) return;
    // Only when it changes: a new tooltip text redraws a tooltip on screen
    const std::string tip = std::format("Recording {}:{:02}: click to stop", seconds / 60, seconds % 60);
    if (!api_btn->tooltip() || tip != api_btn->tooltip()) api_btn->copy_tooltip(tip.c_str());
}

void SearchBar::set_text(const std::string& text){
    // on_text_modified() then fits the bar to it
    input->buffer()->text(text.c_str());
    input->insert_position(input->buffer()->length());
    input->show_insert_position();
}

void SearchBar::on_input_hover(std::function<void(bool hovered)> listener){
    input->hover_listener = std::move(listener);
}

std::string SearchBar::text() const {
    // A copy, which the caller has to free
    char* chars = input->buffer()->text();
    std::string text(chars);
    std::free(chars);
    return text;
}

void SearchBar::collapse(){
    collapsed = true;
    // The one line left shows the start of the text
    input->insert_position(0);
    input->scroll(1, 0);
    fit_text();
}

void SearchBar::resize(int x, int y, int w, int h){
    Fl_Widget::resize(x, y, w, h);
    place_children();
}

int SearchBar::handle(int event){
    // Fonts can be measured now that the window is on screen
    if (event == FL_SHOW){
        const int handled = Fl_Group::handle(event);
        fl_font(input->textfont(), input->textsize());
        line_h = fl_height();
        shown_lines = 0;    // So fit_text() lays the bar out for sure
        fit_text();
        return handled;
    }

    if (event == FL_PUSH && collapsed && Fl::event_inside(input)){
        collapsed = false;
        fit_text();
    }
    return Fl_Group::handle(event);
}

int SearchBar::text_pad() const {
    if (line_h == 0) return 2;
    return std::max(2, (PILL_H - line_h - TEXT_MARGIN_Y) / 2);
}

int SearchBar::height_for(int lines) const {
    return std::max(PILL_H, lines * line_h + TEXT_MARGIN_Y + 2 * text_pad());
}

void SearchBar::place_children(){
    const int X = x(), Y = y(), W = w(), H = h();
    const int btn_w = 80, pad = 3;

    // The API button stays centered as the bar grows. On one line that
    // puts it, a circle as tall as the Search button, in the pill's left
    // end, concentric with its curve.
    const int circle_d = PILL_H - 2 * pad;
    const int circle_x = X + pad;
    api_btn->resize(circle_x, Y + (H - circle_d) / 2, circle_d, circle_d);

    // Faint divider between the API button and the text box, as tall as
    // the text box
    const int divider_x = circle_x + circle_d + 4;
    divider->resize(divider_x, Y + 8, 1, H - 16);

    // The Search button grows with the bar
    const int btn_x = X + W - btn_w - pad;
    search_btn->resize(btn_x, Y + pad, btn_w, H - 2 * pad);

    // Starts a little after the divider and stops a little before the
    // button. Exactly shown_lines tall once the font is known, so no line
    // is cut off at the bottom.
    const int input_x = divider_x + 1 + 6;
    const int input_h = line_h == 0 ? PILL_H - 4 : shown_lines * line_h + TEXT_MARGIN_Y;
    input->resize(input_x, Y + text_pad(), btn_x - 6 - input_x, input_h);
}

void SearchBar::fit_text(){
    if (line_h == 0) return;    // Not shown yet

    const int lines = collapsed ? 1 : input->lines(MAX_LINES);
    if (lines == shown_lines) return;
    shown_lines = lines;

    resize(x(), y(), w(), height_for(lines));
    // A shorter bar uncovers what's under it, and the pill's corners
    // aren't covered by it in the first place
    if (parent()) parent()->redraw();
    if (height_listener) height_listener();
}

void SearchBar::draw(){
    // The pill, stretched to the bar's height
    if (damage() & ~FL_DAMAGE_CHILD) draw_stretched(box(), x(), y(), w(), h(), color(), PILL_H);
    draw_children();
}

void SearchBar::on_api_btn(Fl_Widget* w, void* data){
    SearchBar* self = static_cast<SearchBar*>(data);

    // While recording it's the red button that stops it
    if (self->current_record_state == RecordState::Recording){
        if (self->record_listener) self->record_listener();
        return;
    }

    const int current = static_cast<int>(self->current_api);

    // Record Input, a divider, then one item per API.
    // Rebuilt on every click so the radio check follows the current API.
    // Zeroed first: the extra all-zero item marks the end of the menu.
    constexpr int RECORD = 0, FIRST_API = 1;
    Fl_Menu_Item items[FIRST_API + std::size(API_NAMES) + 1]{};

    // Icon and text side by side. Only needs to live until popup() returns.
    Fl_Multi_Label record_label{};
    record_label.labela = reinterpret_cast<const char*>(mic_icon(false));
    record_label.typea = FL_IMAGE_LABEL;
    record_label.labelb = "  Record Input";
    record_label.typeb = FL_NORMAL_LABEL;
    items[RECORD].multi_label(&record_label);
    items[RECORD].flags = FL_MENU_DIVIDER;

    for (int i = 0; i < static_cast<int>(std::size(API_NAMES)); ++i){
        items[FIRST_API + i].label(API_NAMES[i]);
        items[FIRST_API + i].flags = FL_MENU_RADIO | (i == current ? FL_MENU_VALUE : 0);
    }

    // Opens just below the button. nullptr if the user clicks away.
    const Fl_Menu_Item* picked = items->popup(w->x(), w->y() + w->h(), nullptr, &items[FIRST_API + current]);
    if (picked == nullptr) return;
    if (picked == &items[RECORD]){
        if (self->record_listener) self->record_listener();
        return;
    }

    const int index = static_cast<int>(picked - items) - FIRST_API;
    self->current_api = static_cast<Api>(index);
    w->tooltip(API_NAMES[index]);
}

// Search button and Enter in the text box both end up here.
void SearchBar::on_submit(Fl_Widget*, void* data){
    static_cast<SearchBar*>(data)->do_callback();
}

void SearchBar::on_text_modified(int, int inserted, int deleted, int, const char*, void* data){
    // Also called when the selection changes, which isn't an edit
    if (inserted == 0 && deleted == 0) return;

    SearchBar* self = static_cast<SearchBar*>(data);
    self->collapsed = false;    // Editing shows all of the text again
    self->fit_text();
}
