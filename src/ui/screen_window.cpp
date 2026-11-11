#include "ui/screen_window.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>

// FLTK
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>

#include "core/http.h"
#include "core/ocr_models.h"
#include "core/settings.h"
#include "core/text_reader.h"
#include "ui/bring_to_front.h"
#include "ui/card_window.h"
#include "ui/crop_view.h"
#include "ui/lookup_panels.h"
#include "ui/settings_store.h"
#include "ui/theme.h"

// Like the capture window's, with the screenshot in the waveform's place,
// tall enough that a 16:9 screen fills the window's width, so its text
// can be read in it
static constexpr int MARGIN = 10;
static constexpr int SCREENSHOT_H = 495;
static constexpr int BTN_H = 28;
static constexpr int WHOLE_W = 120;
static constexpr int READ_W = 130;
static constexpr int ANKI_W = 120;
static constexpr int TRANSLATE_W = 100;

ScreenWindow::ScreenWindow(SettingsStore& settings) : settings(settings){
    // win is the current group here, like in MainWindow
    const int inner_w = win.w() - 2 * MARGIN;
    crop_view = new CropView(MARGIN, MARGIN, inner_w, SCREENSHOT_H);
    crop_view->tooltip("Drag around the text to read only that, e.g. the text box");
    crop_view->on_change([this]{
        save_area();
        read();
    });

    const int row_y = MARGIN + SCREENSHOT_H + MARGIN;
    whole_btn = new Fl_Button(MARGIN, row_y, WHOLE_W, BTN_H, "Whole screen");
    whole_btn->box(FL_ROUND_UP_BOX);
    whole_btn->clear_visible_focus();
    whole_btn->tooltip("Read all the text on the screen");
    whole_btn->callback(on_whole, this);

    const int read_x = MARGIN + inner_w - READ_W;
    const int anki_x = read_x - MARGIN - ANKI_W;
    const int translate_x = anki_x - MARGIN - TRANSLATE_W;
    area_info = new Fl_Box(MARGIN + WHOLE_W + MARGIN, row_y, translate_x - WHOLE_W - 3 * MARGIN, BTN_H);
    area_info->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    area_info->labelcolor(DIM);

    translate_btn = new Fl_Button(translate_x, row_y, TRANSLATE_W, BTN_H, "Translate");
    translate_btn->box(FL_ROUND_UP_BOX);
    translate_btn->clear_visible_focus();
    translate_btn->tooltip("Translate this text with DeepL (Ctrl+T)");
    translate_btn->shortcut(FL_CTRL | 't');
    translate_btn->callback(on_translate, this);

    anki_btn = new Fl_Button(anki_x, row_y, ANKI_W, BTN_H, "Add to Anki");
    anki_btn->box(FL_ROUND_UP_BOX);
    anki_btn->clear_visible_focus();
    anki_btn->tooltip("Make a card of the word looked up and this text (Ctrl+N)");
    anki_btn->shortcut(FL_CTRL | 'n');
    anki_btn->callback(on_add_to_anki, this);

    read_btn = new Fl_Button(read_x, row_y, READ_W, BTN_H);
    read_btn->box(FL_ROUND_UP_BOX);
    read_btn->clear_visible_focus();
    read_btn->shortcut(FL_Enter);
    read_btn->tooltip("Read the text in the rectangle again (Enter)");
    read_btn->callback(on_read, this);

    // The rest of the window: the text read, and the lookup panel
    const int panel_y = row_y + BTN_H + MARGIN;
    panels = std::make_unique<LookupPanels>(MARGIN, panel_y, inner_w, win.h() - panel_y - MARGIN);
    panels->on_change([this]{ refresh_buttons(); });

    refresh_buttons();

    // No resizable() on purpose: FLTK then keeps the window at a fixed
    // size, which also makes tiling window managers float it
    win.end();
    win.callback(on_close, this);
}

ScreenWindow::~ScreenWindow(){
    stop_bringing_to_front(win);
    // jobs then goes with the members, stopping and joining each thread
}

void ScreenWindow::open(Image new_screenshot, std::string new_app){
    // Its text would be of the screenshot there was
    cancel_reading();
    screenshot = std::move(new_screenshot);
    app = std::move(new_app);
    crop_view->image(&screenshot);

    // The part kept for this game, if any; all of it otherwise
    const auto& areas = settings.get().screen_text.areas;
    if (const auto it = areas.find(app); it != areas.end()){
        const ScreenArea& a = it->second;
        const double w = screenshot.width, h = screenshot.height;
        crop_view->crop({static_cast<int>(std::lround(a.x * w)), static_cast<int>(std::lround(a.y * h)),
                         static_cast<int>(std::lround(a.w * w)), static_cast<int>(std::lround(a.h * h))});
    }
    refresh_area_info();
    read();

    // Like the capture window: shown anew, it comes up in front on Wayland
    if (win.shown()) win.hide();
    win.show();
    // On Windows that isn't enough, e.g. over a fullscreen game
    bring_to_front(win);
}

void ScreenWindow::refresh_area_info(){
    const std::string game = app.empty() ? "" : std::format(" Kept for {}.", app);
    if (crop_view->whole()) area_info->copy_label("Reading all of it. Drag around the text to read only that.");
    else area_info->copy_label(std::format("Reading the part in the rectangle.{}", game).c_str());
    area_info->redraw();
}

void ScreenWindow::refresh_buttons(){
    read_btn->label(reading ? "Reading..." : "Read");
    if (reading || screenshot.empty()) read_btn->deactivate();
    else read_btn->activate();
    if (crop_view->whole()) whole_btn->deactivate();
    else whole_btn->activate();
    if (panels->text().empty()){
        translate_btn->deactivate();
        anki_btn->deactivate();
    } else {
        translate_btn->activate();
        anki_btn->activate();
    }
}

void ScreenWindow::save_area(){
    if (screenshot.empty()) return;
    const Rect r = crop_view->crop();
    const double w = screenshot.width, h = screenshot.height;
    settings.update([&](Settings& s){
        if (crop_view->whole()) s.screen_text.areas.erase(app);
        else s.screen_text.areas[app] = {r.x / w, r.y / h, r.w / w, r.h / h};
    });
    refresh_area_info();
}

void ScreenWindow::on_whole(Fl_Widget*, void* data){
    auto* self = static_cast<ScreenWindow*>(data);
    // Clamped to the screenshot, so all of it
    self->crop_view->crop({0, 0, 1 << 20, 1 << 20});
    self->crop_view->redraw();
    self->save_area();
    self->read();
}

void ScreenWindow::on_read(Fl_Widget*, void* data){
    static_cast<ScreenWindow*>(data)->read();
}

void ScreenWindow::read(){
    if (screenshot.empty()) return;
    // Its text would be of the rectangle there was
    cancel_reading();

    const unsigned id = next_job_id++;
    reading = id;
    panels->show_note(ocr_models_installed() ? "Reading the screen..."
                                             : "Downloading the models that read the screen (about 30 MB)...");
    refresh_buttons();

    // Readings take turns on the reader: one still running stops at its
    // next line, since it was cancelled above
    jobs.push_back({id, std::jthread([this, id, part = crop(screenshot, crop_view->crop())](std::stop_token stop){
        auto* result = new ReadResult{.win = this, .id = id};
        try {
            const std::lock_guard lock(reader_mutex);
            if (!stop.stop_requested()){
                if (!ocr_models_installed()) download_ocr_models({}, stop);
                if (!reader)
                    reader = std::make_unique<TextReader>(ocr_file_path(OCR_FILES[0]), ocr_file_path(OCR_FILES[1]),
                                                          ocr_file_path(OCR_FILES[2]));
                result->text = reader->read(part, stop);
            }
        } catch (const DownloadCancelled&){
            // Cancelled: not shown anyway
        } catch (const std::exception& e){
            result->error = e.what();
        }
        Fl::awake(on_read_done, result);
    })});
}

void ScreenWindow::cancel_reading(){
    if (!reading) return;
    // Its result still comes, to join the thread, but isn't shown
    for (Job& job : jobs)
        if (job.id == reading) job.thread.request_stop();
    reading = 0;
    refresh_buttons();
}

void ScreenWindow::on_read_done(void* data){
    // unique_ptr so result is deleted when this returns
    std::unique_ptr<ReadResult> result(static_cast<ReadResult*>(data));
    ScreenWindow* self = result->win;

    // Its last act was queueing this, so joining takes no time
    const auto job = std::ranges::find(self->jobs, result->id, &Job::id);
    if (job != self->jobs.end()){
        job->thread.join();
        self->jobs.erase(job);
    }

    // Cancelled, or replaced by another
    if (result->id != self->reading) return;
    self->reading = 0;

    if (!result->error.empty()) self->panels->show_note(std::format("Couldn't read the screen: {}", result->error));
    else if (result->text.empty())
        self->panels->show_note("No text found there. Drag around the text in the screenshot to read it.");
    else {
        self->panels->show_text(result->text);
        self->panels->auto_translate(self->settings.get());
    }
    self->refresh_buttons();
}

void ScreenWindow::on_translate(Fl_Widget*, void* data){
    auto* self = static_cast<ScreenWindow*>(data);
    self->panels->translate(self->settings.get());
}

void ScreenWindow::on_add_to_anki(Fl_Widget*, void* data){
    static_cast<ScreenWindow*>(data)->add_to_anki();
}

void ScreenWindow::add_to_anki(){
    if (panels->text().empty()) return;
    CardDraft draft;
    panels->fill(draft);
    // No audio: the card window says so. A copy of the screenshot, since
    // more than one card can be made from it.
    draft.screenshot = screenshot;

    if (!card_win) card_win = std::make_unique<CardWindow>(settings);
    card_win->open(std::move(draft));
}

void ScreenWindow::on_close(Fl_Widget*, void* data){
    auto* self = static_cast<ScreenWindow*>(data);
    // Escape while reading only cancels that
    if (self->reading && Fl::event() == FL_SHORTCUT && Fl::event_key() == FL_Escape){
        self->cancel_reading();
        self->panels->show_note("Cancelled. Press Enter to read it again.");
        return;
    }

    self->cancel_reading();
    self->win.hide();
    stop_bringing_to_front(self->win);
    // Not kept any longer than it's needed
    self->crop_view->image(nullptr);
    self->screenshot = {};
    self->panels->clear();
}
