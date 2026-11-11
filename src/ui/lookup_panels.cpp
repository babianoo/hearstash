#include "ui/lookup_panels.h"

#include <exception>
#include <format>
#include <memory>
#include <thread>

// FLTK
#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Box.H>

#include "core/deepl.h"
#include "core/lookup_text.h"
#include "core/settings.h"
#include "ui/card_window.h"
#include "ui/jisho_format.h"
#include "ui/smooth_scroll.h"
#include "ui/theme.h"
#include "ui/wrapped_text.h"

// Space between the text and the line, and the line and the panel
static constexpr int GAP = 10;
// The text's width; the panel takes the rest
static constexpr int TEXT_W = 360;

// The text is what the window is for, so it's big. Notes, like what to
// press, are smaller and grey.
static const TextStyle TEXT{FL_HELVETICA, 24};
static const TextStyle NOTE{FL_HELVETICA, 14, DIM};

LookupPanels::LookupPanels(int x, int y, int w, int h){
    // Both blend into the window, like the main window's results
    text_scroll = new SmoothScroll(x, y, TEXT_W, h);
    text_scroll->box(FL_FLAT_BOX);
    text_scroll->color(FL_BACKGROUND_COLOR);
    text_view = new WrappedText(x, y, text_scroll->content_w());
    text_view->color(FL_BACKGROUND_COLOR);
    text_view->on_select([this]{ look_up_selection(); });
    text_scroll->end();

    const int line_x = x + TEXT_W + GAP;
    Fl_Box* line = new Fl_Box(line_x, y, 1, h);
    line->box(FL_FLAT_BOX);
    line->color(sidebar_color());

    const int panel_x = line_x + 1 + GAP;
    lookup_scroll = new SmoothScroll(panel_x, y, x + w - panel_x, h);
    lookup_scroll->box(FL_FLAT_BOX);
    lookup_scroll->color(FL_BACKGROUND_COLOR);
    lookup_view = new WrappedText(panel_x, y, lookup_scroll->content_w());
    lookup_view->color(FL_BACKGROUND_COLOR);
    lookup_scroll->end();
}

void LookupPanels::show_text(const std::string& text){
    set_text({{text, TEXT}});
    text_ = text;
    entry.reset();
    keyword.clear();
    // A lookup still running is of the text there was
    ++latest;
    set_panel({{"Select a word in the text to look it up.", NOTE}});
    if (change_listener) change_listener();
}

void LookupPanels::show_note(const std::string& note){
    if (note.empty()) set_text({});
    else set_text({{note, NOTE}});
    const bool changed = !text_.empty();
    text_.clear();
    entry.reset();
    keyword.clear();
    ++latest;
    set_panel({});
    if (changed && change_listener) change_listener();
}

void LookupPanels::set_text(const std::vector<TextSpan>& spans){
    text_scroll->scroll_to_top();
    text_view->text(spans);
    // Its scrollbar depends on the new text's height
    text_scroll->redraw();
}

void LookupPanels::set_panel(const std::vector<TextSpan>& spans){
    lookup_scroll->scroll_to_top();
    lookup_view->text(spans);
    lookup_scroll->redraw();
}

void LookupPanels::look_up_selection(){
    const std::string selected = lookup_text(text_view->selected_text());
    // Nothing worth a search, or a note selected
    if (selected.empty() || text_.empty()) return;

    const unsigned id = ++latest;
    set_panel({{std::format("Looking up {}...", selected), NOTE}});

    // Like the main window's searches: the request can take seconds, so
    // it's made on a thread that only hands its result back. Nothing
    // waits for it.
    std::thread([this, id, selected]{
        auto* result = new LookupResult{.panels = this, .id = id, .keyword = selected};
        try {
            result->entries = jisho_search(selected);
        } catch (const std::exception& e){
            result->error = e.what();
        }
        Fl::awake(on_lookup_done, result);
    }).detach();
}

void LookupPanels::on_lookup_done(void* data){
    // unique_ptr so result is deleted when this returns
    std::unique_ptr<LookupResult> result(static_cast<LookupResult*>(data));
    LookupPanels* self = result->panels;
    // A newer lookup started since, or the text changed
    if (result->id != self->latest) return;

    self->keyword = result->keyword;
    if (result->entries.empty()) self->entry.reset();
    else self->entry.emplace(result->entries.front());

    if (!result->error.empty())
        self->set_panel({{std::format("Couldn't look up {}: {}", result->keyword, result->error), NOTE}});
    else if (result->entries.empty())
        // Jisho knows dictionary forms, and only some inflected ones
        self->set_panel({{std::format("Nothing found for {}. Try selecting less, or the word's dictionary form.",
                                      result->keyword), NOTE}});
    else self->set_panel(format_entries(result->entries));
}

void LookupPanels::translate(const Settings& settings){
    if (text_.empty()) return;

    // Copied for the thread, since the settings may change while it runs
    std::string key = effective_deepl_key(settings);
    if (key.empty()){
        // Modal: waits for OK before returning
        fl_alert("No DeepL API key found.\n\n"
                 "Input the DeepL key in the settings or "
                 "set the DEEPL_API_KEY environment variable to use DeepL.");
        return;
    }

    // Takes the panel's place from a lookup still running. The word
    // looked up is still what a card gets.
    const unsigned id = ++latest;
    set_panel({{"Translating...", NOTE}});

    // Like a lookup: on a thread that only hands its result back
    std::thread([this, id, text = text_, key = std::move(key)]{
        auto* result = new TranslationResult{.panels = this, .id = id};
        try {
            result->translation = deepl_translate(text, key);
        } catch (const std::exception& e){
            result->error = e.what();
        }
        Fl::awake(on_translation_done, result);
    }).detach();
}

void LookupPanels::on_translation_done(void* data){
    // unique_ptr so result is deleted when this returns
    std::unique_ptr<TranslationResult> result(static_cast<TranslationResult*>(data));
    LookupPanels* self = result->panels;
    // A lookup started since, or the text changed
    if (result->id != self->latest) return;

    if (!result->error.empty()) self->set_panel({{std::format("Couldn't translate: {}", result->error), NOTE}});
    else self->set_panel({{result->translation}});
}

// A dictionary entry's meanings as the card window shows them and
// format_card() reads them: one numbered line each, under a line with
// their parts of speech, e.g.
//   [Noun]
//   1. prey; catch; kill; game
static std::string meanings(const JishoEntry& entry){
    std::string text;
    // The last line of them written. A sense that names none has the ones
    // before it.
    std::string tags;
    int n = 1;
    for (const JishoSense& sense : entry.senses){
        std::string line;
        for (const std::string& part : sense.parts_of_speech){
            if (!line.empty()) line += ' ';
            line += '[' + part + ']';
        }
        if (!line.empty() && line != tags){
            tags = line;
            text += line + '\n';
        }
        text += std::format("{}. ", n++);
        for (size_t i = 0; i < sense.definitions.size(); ++i){
            if (i > 0) text += "; ";
            text += sense.definitions[i];
        }
        text += '\n';
    }
    if (!text.empty()) text.pop_back();
    return text;
}

void LookupPanels::fill(CardDraft& draft) const {
    draft.sentence = text_;
    // What was selected to look up, as it's written in the text
    draft.highlight = keyword;
    if (entry && !entry->forms.empty()){
        // Kana-only words have just the reading, which then is the word
        const JishoForm& form = entry->forms.front();
        draft.word = form.word.empty() ? form.reading : form.word;
        draft.reading = form.reading;
        draft.meaning = meanings(*entry);
    }
}
