#include "ui/stash_window.h"

#include <algorithm>
#include <ctime>
#include <exception>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// FLTK
#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Image.H>

#include "core/image.h"
#include "core/local_time.h"
#include "core/stash.h"
#include "ui/fit_line.h"
#include "ui/smooth_scroll.h"
#include "ui/stash_store.h"
#include "ui/theme.h"

// Space around and between things
static constexpr int MARGIN = 10;
static constexpr int BTN_W = 90, BTN_H = 28;
static constexpr int CLEAR_W = 100;
// A row: the thumbnail with space around it, then the text
static constexpr int PAD = 8;
static constexpr int ROW_H = STASH_THUMBNAIL_HEIGHT + 2 * PAD;
// Wide enough for a 16:9 thumbnail; a narrower one is centred in it
static constexpr int THUMB_W = STASH_THUMBNAIL_HEIGHT * 16 / 9;

static const Fl_Color ERROR_COLOR = fl_rgb_color(200, 40, 40);

// bytes as "2.9 MB", or "640 KB" under one
static std::string size_text(std::uintmax_t bytes){
    if (bytes < 1000 * 1000) return std::format("{} KB", (bytes + 999) / 1000);
    return std::format("{:.1f} MB", static_cast<double>(bytes) / (1000 * 1000));
}

// When, shorter the more recent: "Today 21:04", "Yesterday 21:04",
// "Mon 21:04", "Oct 5 21:04"
static std::string when_text(std::int64_t time, const std::tm& now){
    const std::tm t = local_time(time);
    const auto days_ago = (day_of(now) - day_of(t)).count();
    const std::string clock = format_time(t, "%H:%M");
    if (days_ago == 0) return "Today " + clock;
    if (days_ago == 1) return "Yesterday " + clock;
    if (days_ago > 1 && days_ago < 7) return format_time(t, "%A ") + clock;
    // Not %e, which pads the day with a space
    return format_time(t, "%b ") + std::to_string(t.tm_mday) + " " + clock;
}

// The rows: one per stashed line, at least as tall as the scroll, so the
// note shown without any can sit in its middle. Thumbnails are read the
// first time their row is drawn.
class StashRows : public Fl_Widget {
public:
    StashRows(int x, int y, int w, int h, const StashStore& stash) : Fl_Widget(x, y, w, h), stash(stash){
        // Draws its own background, since it can be redrawn without its
        // parent
        box(FL_FLAT_BOX);
        color(FL_BACKGROUND2_COLOR);
    }

    // Index into the stash's entries, -1 for none
    int selected = -1;
    std::function<void()> select_listener;
    std::function<void()> open_listener;
    std::function<void()> delete_listener;

    // After the stash changed: keeps the same line selected if it's still
    // there, and forgets the thumbnails of lines that are gone
    void refresh(){
        const auto& entries = stash.entries();
        const auto found = std::ranges::find(entries, selected_id, &StashEntry::id);
        selected = found == entries.end() ? -1 : static_cast<int>(found - entries.begin());
        if (selected < 0) selected_id.clear();
        std::erase_if(thumbnails, [&](const auto& item){
            return std::ranges::find(entries, item.first, &StashEntry::id) == entries.end();
        });
        redraw();
        if (select_listener) select_listener();
    }

    // Clamped to the rows there are, and scrolled into view. Does nothing
    // without rows.
    void select(int row){
        const auto& entries = stash.entries();
        if (entries.empty()) return;
        row = std::clamp(row, 0, static_cast<int>(entries.size()) - 1);
        // The scroll this is in
        static_cast<SmoothScroll*>(parent())->scroll_into_view(row * ROW_H, ROW_H);
        if (row == selected) return;
        selected = row;
        selected_id = entries[row].id;
        redraw();
        if (select_listener) select_listener();
    }

    // Up, Down, Home and End move the selection, Enter opens the selected
    // line and Delete deletes it. Returns whether it took key.
    bool key(int key){
        const int count = static_cast<int>(stash.entries().size());
        if (count == 0) return false;
        switch (key){
        case FL_Up: select(selected < 0 ? 0 : selected - 1); return true;
        case FL_Down: select(selected + 1); return true;
        case FL_Home: select(0); return true;
        case FL_End: select(count - 1); return true;
        case FL_Enter:
        case FL_KP_Enter: return tell(open_listener);
        case FL_Delete:
        case FL_BackSpace: return tell(delete_listener);
        }
        return false;
    }

    int handle(int event) override {
        switch (event){
        case FL_PUSH: {
            if (Fl::event_button() != FL_LEFT_MOUSE) break;
            take_focus();    // For the keys
            const int row = row_at(Fl::event_y());
            if (row < 0) return 1;
            // A double-click's second click is on the row its first one
            // selected
            const bool again = Fl::event_clicks() > 0 && row == selected;
            select(row);
            if (again) tell(open_listener);
            return 1;
        }
        case FL_FOCUS:
        case FL_UNFOCUS:
            return 1;    // Accepts keyboard focus
        case FL_KEYBOARD:
            return key(Fl::event_key()) ? 1 : 0;
        }
        return Fl_Widget::handle(event);
    }

    void draw() override {
        draw_box();
        const auto& entries = stash.entries();
        if (entries.empty()){
            fl_font(FL_HELVETICA_ITALIC, FL_NORMAL_SIZE);
            fl_color(DIM);
            fl_draw("Nothing stashed. Press the stash shortcut while watching to keep a line for later.",
                    x() + MARGIN, y(), w() - 2 * MARGIN, h(), FL_ALIGN_CENTER | FL_ALIGN_WRAP);
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
    const StashStore& stash;
    // The selected line's id, to find it again once the stash changes
    std::string selected_id;

    // A thumbnail, with the FLTK image that borrows its pixels. Empty for
    // a line without one.
    struct Thumbnail {
        Image image;
        std::unique_ptr<Fl_RGB_Image> fl;
    };
    // By id. Mutable: filled while drawing.
    mutable std::map<std::string, Thumbnail> thumbnails;

    // Row at window y, or -1 if there's none there
    int row_at(int y) const {
        if (y < this->y()) return -1;
        const int row = (y - this->y()) / ROW_H;
        return row < static_cast<int>(stash.entries().size()) ? row : -1;
    }

    bool tell(const std::function<void()>& listener) const {
        if (selected < 0 || !listener) return false;
        listener();
        return true;
    }

    const Thumbnail& thumbnail(const StashEntry& entry) const {
        auto found = thumbnails.find(entry.id);
        if (found != thumbnails.end()) return found->second;
        Thumbnail thumb;
        thumb.image = load_stash_thumbnail(stash.dir(), entry);
        if (!thumb.image.empty())
            thumb.fl = std::make_unique<Fl_RGB_Image>(thumb.image.rgb.data(), thumb.image.width, thumb.image.height, 3);
        // The Fl_RGB_Image points into image's pixels, which don't move
        // when the Thumbnail does: a vector's buffer goes with it
        return thumbnails.emplace(entry.id, std::move(thumb)).first->second;
    }

    void draw_row(int i, const std::tm& now) const {
        const StashEntry& entry = stash.entries()[static_cast<size_t>(i)];
        const int top = y() + i * ROW_H;
        if (i == selected)
            fl_draw_box(FL_FLAT_BOX, x(), top, w(), ROW_H, fl_color_average(ACCENT, FL_BACKGROUND2_COLOR, 0.2f));
        // A line between rows
        fl_color(fl_color_average(FL_BLACK, FL_BACKGROUND2_COLOR, 0.1f));
        fl_xyline(x(), top + ROW_H - 1, x() + w() - 1);

        // The thumbnail, or a grey box where it would be
        const int thumb_x = x() + PAD, thumb_y = top + PAD;
        const Thumbnail& thumb = thumbnail(entry);
        if (thumb.fl){
            const int image_w = std::min(thumb.image.width, THUMB_W);
            thumb.fl->draw(thumb_x + (THUMB_W - image_w) / 2, thumb_y, image_w, thumb.image.height);
        } else {
            fl_draw_box(FL_FLAT_BOX, thumb_x, thumb_y, THUMB_W, STASH_THUMBNAIL_HEIGHT,
                        fl_color_average(FL_BLACK, FL_BACKGROUND2_COLOR, 0.08f));
            fl_font(FL_HELVETICA, 12);
            fl_color(DIM);
            fl_draw("No screenshot", thumb_x, thumb_y, THUMB_W, STASH_THUMBNAIL_HEIGHT, FL_ALIGN_CENTER);
        }

        const int text_x = thumb_x + THUMB_W + 2 * PAD;
        const int text_w = x() + w() - PAD - text_x;
        fl_push_clip(text_x, top, text_w, ROW_H);
        fl_font(FL_HELVETICA_BOLD, FL_NORMAL_SIZE + 1);
        fl_color(FL_FOREGROUND_COLOR);
        int baseline = top + PAD + fl_height() - fl_descent();
        // Not fl_draw()'s boxed form, which would take '@' for a symbol
        fl_draw(when_text(entry.time, now).c_str(), text_x, baseline);

        // What it says, if it was transcribed when stashed
        fl_font(FL_HELVETICA, FL_NORMAL_SIZE);
        baseline += fl_height() + 4;
        if (stash.transcribing(entry.id)){
            fl_font(FL_HELVETICA_ITALIC, FL_NORMAL_SIZE);
            fl_color(DIM);
            fl_draw("Transcribing...", text_x, baseline);
            baseline += fl_height() + 2;
        } else if (!entry.transcript.empty()){
            fl_color(FL_FOREGROUND_COLOR);
            fl_draw(fit_line(entry.transcript, text_w).c_str(), text_x, baseline);
            baseline += fl_height() + 2;
        } else if (!entry.transcript_error.empty()){
            fl_color(DIM);
            fl_draw(fit_line(std::format("Not transcribed: {}", entry.transcript_error), text_w).c_str(), text_x, baseline);
            baseline += fl_height() + 2;
        }

        fl_font(FL_HELVETICA, FL_NORMAL_SIZE);
        fl_color(DIM);
        const std::string where = entry.app.empty() ? "" : std::format("In {} · ", entry.app);
        fl_draw(std::format("{}{:.0f} s · {}", where, entry.seconds, size_text(entry.bytes)).c_str(), text_x, baseline);
        if (!entry.screenshot_error.empty()){
            baseline += fl_height() + 2;
            fl_font(FL_HELVETICA, 12);
            fl_draw(fit_line(std::format("No screenshot: {}", entry.screenshot_error), text_w).c_str(), text_x, baseline);
        }
        fl_pop_clip();
    }
};

StashWindow::StashWindow(StashStore& stash, std::function<void(const StashEntry&)> open)
    : stash(stash), open(std::move(open)){
    // win is the current group here, like in MainWindow
    const int inner_w = win.w() - 2 * MARGIN;
    const int clear_x = MARGIN + inner_w - CLEAR_W;
    const int delete_x = clear_x - MARGIN - BTN_W;
    const int open_x = delete_x - MARGIN - BTN_W;

    summary = new Fl_Box(MARGIN, MARGIN, open_x - 2 * MARGIN, BTN_H);
    summary->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);

    open_btn = new Fl_Button(open_x, MARGIN, BTN_W, BTN_H, "Open");
    open_btn->box(FL_ROUND_UP_BOX);
    open_btn->clear_visible_focus();
    open_btn->tooltip("Open the line in the capture window (Enter)");
    open_btn->callback(on_open, this);

    delete_btn = new Fl_Button(delete_x, MARGIN, BTN_W, BTN_H, "Delete");
    delete_btn->box(FL_ROUND_UP_BOX);
    delete_btn->clear_visible_focus();
    delete_btn->tooltip("Delete the line (Delete)");
    delete_btn->callback(on_delete, this);

    clear_btn = new Fl_Button(clear_x, MARGIN, CLEAR_W, BTN_H, "Clear All");
    clear_btn->box(FL_ROUND_UP_BOX);
    clear_btn->clear_visible_focus();
    clear_btn->tooltip("Delete every stashed line");
    clear_btn->callback(on_clear, this);

    // Only shown while the last stash failed
    const int error_y = MARGIN + BTN_H + 4;
    error_box = new Fl_Box(MARGIN, error_y, inner_w, 20);
    error_box->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    error_box->labelcolor(ERROR_COLOR);
    error_box->labelsize(12);

    const int list_y = error_y + 20 + 4;
    scroll = new SmoothScroll(MARGIN, list_y, inner_w, win.h() - list_y - MARGIN);
    scroll->box(FL_DOWN_BOX);
    scroll->color(FL_BACKGROUND2_COLOR);
    rows = new StashRows(MARGIN + Fl::box_dx(scroll->box()), list_y + Fl::box_dy(scroll->box()),
                         scroll->content_w(), scroll->h(), stash);
    rows->select_listener = [this]{
        if (rows->selected < 0){
            open_btn->deactivate();
            delete_btn->deactivate();
        } else {
            open_btn->activate();
            delete_btn->activate();
        }
    };
    rows->open_listener = [this]{ open_selected(); };
    rows->delete_listener = [this]{ delete_selected(); };
    scroll->end();

    // No resizable() on purpose: FLTK then keeps the window at a fixed
    // size, which also makes tiling window managers float it
    win.end();

    stash_listener = stash.add_listener([this]{ refresh(); });
    refresh();
}

StashWindow::~StashWindow(){
    stash.remove_listener(stash_listener);
}

void StashWindow::show(){
    // Lines may have been added or deleted outside the app
    win.show();
    rows->take_focus();
    // The first, newest line, so Enter opens it straight away
    if (rows->selected < 0) rows->select(0);
}

void StashWindow::refresh(){
    const auto& entries = stash.entries();
    if (entries.empty()) summary->copy_label("Nothing stashed");
    else summary->copy_label(std::format("{} {} · {}", entries.size(), entries.size() == 1 ? "line" : "lines",
                                         size_text(stash.bytes())).c_str());
    summary->redraw();

    if (stash.last_error().empty()) error_box->hide();
    else {
        error_box->copy_label(std::format("The last stash didn't work: {}", stash.last_error()).c_str());
        error_box->show();
    }

    if (entries.empty()) clear_btn->deactivate();
    else clear_btn->activate();
    fit_rows();
    // Also says whether there's a line selected for Open and Delete
    rows->refresh();
}

void StashWindow::fit_rows(){
    const int count = static_cast<int>(stash.entries().size());
    // Stops before the scrollbar
    rows->size(scroll->content_w(), std::max(count * ROW_H, scroll->h() - Fl::box_dh(scroll->box())));
    // Its scrollbar depends on the rows' height
    scroll->redraw();
}

void StashWindow::on_open(Fl_Widget*, void* data){
    static_cast<StashWindow*>(data)->open_selected();
}

void StashWindow::on_delete(Fl_Widget*, void* data){
    static_cast<StashWindow*>(data)->delete_selected();
}

void StashWindow::on_clear(Fl_Widget*, void* data){
    static_cast<StashWindow*>(data)->clear();
}

void StashWindow::open_selected(){
    if (rows->selected < 0) return;
    // A copy: opening it may change the stash, e.g. a card made at once
    const StashEntry entry = stash.entries()[static_cast<size_t>(rows->selected)];
    open(entry);
}

void StashWindow::delete_selected(){
    if (rows->selected < 0) return;
    const int row = rows->selected;
    const std::string id = stash.entries()[static_cast<size_t>(row)].id;
    try {
        stash.remove(id);
    } catch (const std::exception& e){
        // Modal: waits for OK before returning
        fl_alert("Couldn't delete the stashed line:\n\n%s", e.what());
    }
    // The one that took its place, or the new last one
    rows->select(row);
}

void StashWindow::clear(){
    const size_t count = stash.entries().size();
    if (count == 0) return;
    // Modal: waits for an answer before returning
    const std::string question = std::format("Delete all {} stashed {}? Their audio and screenshots can't be "
                                             "brought back.", count, count == 1 ? "line" : "lines");
    if (fl_choice("%s", "Cancel", "Delete All", nullptr, question.c_str()) != 1) return;
    try {
        stash.clear();
    } catch (const std::exception& e){
        fl_alert("Couldn't delete every stashed line:\n\n%s", e.what());
    }
}
