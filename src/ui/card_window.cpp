#include "ui/card_window.h"

#include <chrono>
#include <cmath>
#include <exception>
#include <format>
#include <utility>

// FLTK
#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>

#include "core/anki_connect.h"
#include "core/card_format.h"
#include "core/hearstash_note_type.h"
#include "core/media_encoding.h"
#include "core/settings.h"
#include "ui/clipboard_input.h"
#include "ui/crop_view.h"
#include "ui/settings_store.h"
#include "ui/theme.h"

static constexpr int MARGIN = 10;
// The labels' column, left of the fields
static constexpr int LABEL_W = 80;
static constexpr int ROW_GAP = 6;
static constexpr int BTN_W = 90, BTN_H = 28;
static constexpr int CROP_H = 330;

// Pictures on cards are no taller than this, which keeps the collection
// small and is plenty on a phone
static constexpr int PICTURE_MAX_H = 480;
// Every card the app adds gets it, so they can be found in Anki
static constexpr const char* TAG = "hearstash";

// A label on the left, at the field's first line
static void label(int y, const char* text){
    Fl_Box* box = new Fl_Box(MARGIN, y, LABEL_W, PILL_H, text);
    box->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
}

CardWindow::CardWindow(SettingsStore& settings) : settings(settings){
    // win is the current group here, like in MainWindow
    const int field_x = MARGIN + LABEL_W, field_w = win.w() - field_x - MARGIN;
    int y = MARGIN;

    const auto add_field = [&](const char* name, int lines){
        label(y, name);
        auto* input = new ClipboardInput(field_x, y, field_w, lines == 1 ? PILL_H : 8 + lines * 20);
        if (lines > 1) input->type(FL_MULTILINE_INPUT_WRAP);
        y += input->h() + ROW_GAP;
        return input;
    };
    word_input = add_field("Word", 1);
    reading_input = add_field("Reading", 1);
    meaning_input = add_field("Meaning", 4);
    sentence_input = add_field("Sentence", 2);
    highlight_input = add_field("Highlight", 1);
    highlight_input->tooltip("The part of the sentence to show bigger and in color, as it's written there");

    label(y, "Audio");
    audio_info = new Fl_Box(field_x, y, field_w, PILL_H);
    audio_info->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    audio_info->labelcolor(DIM);
    y += PILL_H + ROW_GAP;

    label(y, "Picture");
    crop_view = new CropView(field_x, y, field_w, CROP_H);
    y += CROP_H + ROW_GAP;

    const int buttons_y = win.h() - MARGIN - BTN_H;
    destination = new Fl_Box(MARGIN, buttons_y, win.w() - 3 * MARGIN - 2 * BTN_W, BTN_H);
    destination->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    destination->labelsize(12);
    destination->labelcolor(DIM);

    auto* cancel_btn = new Fl_Button(win.w() - 2 * MARGIN - 2 * BTN_W, buttons_y, BTN_W, BTN_H, "Cancel");
    cancel_btn->box(FL_ROUND_UP_BOX);
    cancel_btn->clear_visible_focus();
    cancel_btn->callback(on_close, this);

    add_btn = new Fl_Button(win.w() - MARGIN - BTN_W, buttons_y, BTN_W, BTN_H, "Add");
    add_btn->box(FL_ROUND_UP_BOX);
    add_btn->clear_visible_focus();
    add_btn->tooltip("Add the card to Anki (Ctrl+Enter)");
    add_btn->shortcut(FL_CTRL | FL_Enter);
    add_btn->callback(on_add, this);

    // No resizable() on purpose: FLTK then keeps the window at a fixed
    // size, which also makes tiling window managers float it
    win.end();
    win.callback(on_close, this);
}

void CardWindow::open(CardDraft new_draft, std::function<void()> new_on_added){
    draft = std::move(new_draft);
    on_added = std::move(new_on_added);
    word_input->value(draft.word.c_str());
    reading_input->value(draft.reading.c_str());
    meaning_input->value(draft.meaning.c_str());
    meaning_input->tooltip(draft.glossary_html.empty() ? nullptr
        : "Left as it is, the card gets the dictionary's whole entry, with its example sentences.\n"
          "Edit it and the card gets what's typed here instead.");
    sentence_input->value(draft.sentence.c_str());
    highlight_input->value(draft.highlight.c_str());

    if (draft.audio.empty()) audio_info->copy_label("None");
    else audio_info->copy_label(std::format("{:.1f} s, the selection in the capture window",
                                            static_cast<double>(draft.audio.size()) / draft.rate).c_str());

    crop_view->image(draft.screenshot.empty() ? nullptr : &draft.screenshot);
    if (draft.screenshot.empty()) crop_view->copy_tooltip(draft.screenshot_error.c_str());
    else crop_view->tooltip("Drag to choose the part of the screenshot to keep");
    restore_crop();
    refresh_destination();

    // Wayland doesn't let an app raise a window that's already shown, but
    // a window that's shown anew comes up in front, with the focus
    if (win.shown()) win.hide();
    win.show();
    word_input->take_focus();
}

void CardWindow::refresh_destination(){
    const AnkiSettings& anki = settings.get().anki;
    if (anki.deck.empty() || anki.note_type.empty()){
        destination->copy_label("Choose a deck and note type in Settings > Anki first.");
        destination->labelcolor(fl_rgb_color(200, 40, 40));
        add_btn->deactivate();
    } else {
        destination->copy_label(std::format("To {}, as {}", anki.deck, anki.note_type).c_str());
        destination->labelcolor(DIM);
        add_btn->activate();
    }
    destination->redraw();
}

void CardWindow::restore_crop(){
    const AnkiSettings& anki = settings.get().anki;
    if (draft.screenshot.empty() || anki.crop_w <= 0) return;
    const double w = draft.screenshot.width, h = draft.screenshot.height;
    crop_view->crop({static_cast<int>(std::lround(anki.crop_x * w)), static_cast<int>(std::lround(anki.crop_y * h)),
                     static_cast<int>(std::lround(anki.crop_w * w)), static_cast<int>(std::lround(anki.crop_h * h))});
}

void CardWindow::save_crop(){
    if (draft.screenshot.empty()) return;
    const Rect r = crop_view->crop();
    const double w = draft.screenshot.width, h = draft.screenshot.height;
    settings.update([&](Settings& s){
        s.anki.crop_x = r.x / w;
        s.anki.crop_y = r.y / h;
        s.anki.crop_w = r.w / w;
        s.anki.crop_h = r.h / h;
    });
}

void CardWindow::on_add(Fl_Widget*, void* data){
    static_cast<CardWindow*>(data)->add();
}

void CardWindow::add(){
    const AnkiSettings& anki = settings.get().anki;
    CardContent card{.word = word_input->value(), .reading = reading_input->value(),
                     .meaning = meaning_input->value(), .glossary_html = {}, .sentence = sentence_input->value(),
                     .highlight = highlight_input->value(), .audio_file = {}, .picture_file = {}};
    // Left as the dictionary gave it, the card gets the dictionary's whole
    // entry. Edited, it gets what's typed: the entry can't be edited as
    // text.
    if (card.meaning == draft.meaning) card.glossary_html = draft.glossary_html;

    // Named by the time, so they don't replace another card's in Anki's
    // media folder
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    try {
        // Quick, since Anki is on this machine, but the files take a
        // moment to send
        win.cursor(FL_CURSOR_WAIT);
        Fl::flush();
        // The first card of the app's own note type adds it to Anki
        if (anki.note_type == HEARSTASH_NOTE_TYPE) ensure_hearstash_note_type();
        // Stored first, so the note can show them. If the note is then
        // refused, they stay in the media folder unused until Anki's Check
        // Media removes them.
        if (!anki.audio_field.empty() && !draft.audio.empty()){
            card.audio_file = std::format("{}_{}.wav", TAG, stamp);
            anki_store_media(card.audio_file, encode_wav(draft.audio, draft.rate));
        }
        if (!anki.picture_field.empty() && !draft.screenshot.empty()){
            card.picture_file = std::format("{}_{}.jpg", TAG, stamp);
            const Image picture = shrink_to_height(crop(draft.screenshot, crop_view->crop()), PICTURE_MAX_H);
            anki_store_media(card.picture_file, encode_jpeg(picture, anki.picture_quality));
        }
        anki_add_note({.deck = anki.deck, .model = anki.note_type, .fields = format_card(card, anki), .tags = {TAG}});
        win.cursor(FL_CURSOR_DEFAULT);
    } catch (const std::exception& e){
        win.cursor(FL_CURSOR_DEFAULT);
        // Modal: waits for OK before returning. The window stays, so
        // nothing typed is lost.
        fl_alert("Couldn't add the card:\n\n%s", e.what());
        return;
    }

    // Before close() forgets it. A copy: what it calls may open this
    // window again with another card, replacing on_added.
    if (const std::function<void()> added = on_added) added();
    close();
}

void CardWindow::on_close(Fl_Widget*, void* data){
    static_cast<CardWindow*>(data)->close();
}

void CardWindow::close(){
    // Kept even when the card isn't added: it was moved to the video
    save_crop();
    win.hide();
    // Not kept any longer than it's needed
    crop_view->image(nullptr);
    draft = {};
    on_added = {};
}
