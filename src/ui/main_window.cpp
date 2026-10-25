#include "ui/main_window.h"

#include <format>
#include <stdexcept>
#include <string_view>
#include <thread>

// FLTK
#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_SVG_Image.H>

#include "core/deepl.h"
#include "core/settings.h"
#include "ui/clipboard.h"
#include "ui/icons.h"
#include "ui/search_bar.h"
#include "ui/settings_store.h"
#include "ui/settings_window.h"
#include "ui/theme.h"
#include "ui/smooth_scroll.h"
#include "ui/wrapped_text.h"

// Space around the search bar and results box
static constexpr int MARGIN = 10;

// Looks of the parts of a Jisho result
static const Fl_Color DIM = fl_rgb_color(70, 70, 70);
static const TextStyle WORD{FL_HELVETICA_BOLD, 24};
static const TextStyle READING{FL_HELVETICA, 16, DIM};
static const TextStyle COMMON{FL_HELVETICA_BOLD, 12, fl_rgb_color(40, 120, 40)};
static const TextStyle NUMBER{FL_HELVETICA, 14, DIM};
static const TextStyle DEFINITION{};
static const TextStyle PART_OF_SPEECH{FL_HELVETICA_ITALIC, 12, DIM};
static const TextStyle OTHER_FORMS{FL_HELVETICA, 13, DIM};

static std::string join(const std::vector<std::string>& parts, std::string_view separator){
    std::string joined;
    for (size_t i = 0; i < parts.size(); ++i){
        if (i > 0) joined += separator;
        joined += parts[i];
    }
    return joined;
}

// The results, styled a bit like jisho.org:
//
//   兄弟  きょうだい  common word          (big bold word, grey reading)
//       1. brothers and sisters; siblings  Noun
//       Other forms: 兄弟 (けいてい)
static std::vector<TextSpan> format_entries(const std::vector<JishoEntry>& entries){
    std::vector<TextSpan> spans;
    for (const JishoEntry& entry : entries){
        if (!entry.forms.empty()){
            // Kana-only words have just the reading, which then is the word
            const JishoForm& main = entry.forms.front();
            if (main.word.empty()){
                spans.push_back({main.reading, WORD});
            } else {
                spans.push_back({main.word, WORD});
                spans.push_back({"  " + main.reading, READING});
            }
        }
        if (entry.is_common) spans.push_back({"  common word", COMMON});
        spans.push_back({"\n"});

        int n = 1;
        for (const JishoSense& sense : entry.senses){
            spans.push_back({std::format("    {}. ", n++), NUMBER});
            spans.push_back({join(sense.definitions, "; "), DEFINITION});
            if (!sense.parts_of_speech.empty())
                spans.push_back({"  " + join(sense.parts_of_speech, ", "), PART_OF_SPEECH});
            spans.push_back({"\n"});
        }

        if (entry.forms.size() > 1){
            std::vector<std::string> others;
            for (size_t i = 1; i < entry.forms.size(); ++i){
                const JishoForm& form = entry.forms[i];
                if (form.word.empty()) others.push_back(form.reading);
                else others.push_back(std::format("{} ({})", form.word, form.reading));
            }
            spans.push_back({"    Other forms: " + join(others, ", ") + '\n', OTHER_FORMS});
        }
        spans.push_back({"\n"});
    }
    return spans;
}

MainWindow::MainWindow(SettingsStore& settings, ModelDownloader& downloader)
    : settings(settings), downloader(downloader){
    // win is constructed (as a member) before this body runs, so it's
    // the current group here: widgets created below are added to it.

    // Centered, a bit above the middle
    const int bar_w = 600;
    const int bar_x = (win.w() - bar_w) / 2, bar_y = win.h() * 2 / 5;
    search_bar = new SearchBar(bar_x, bar_y, bar_w);
    search_bar->callback(on_search, this);

    // Fills the space under the bar, same width
    const int results_y = bar_y + search_bar->h() + MARGIN;
    results = new SmoothScroll(bar_x, results_y, bar_w, win.h() - results_y - MARGIN);
    // Blends into the window: no border, window's color. Still a flat box,
    // not FL_NO_BOX, since it has to repaint what scrolling uncovers.
    results->box(FL_FLAT_BOX);
    results->color(FL_BACKGROUND_COLOR);
    // Inside the box's border, and stops before the scrollbar
    results_text = new WrappedText(bar_x + Fl::box_dx(results->box()),
                                   results_y + Fl::box_dy(results->box()),
                                   results->content_w());
    results_text->color(FL_BACKGROUND_COLOR);
    results->end();
    results->hide();    // Shown by move_bar_to_top() on the first search

    // Settings button in the top-right corner, as tall as the search bar so
    // it lines up with it once the bar moves to the top.
    Fl_Button* settings_btn = new Fl_Button(win.w() - MARGIN - 25, MARGIN, PILL_H, PILL_H);
    // Same box as the Search button; on a square button it's a circle.
    // Pressed it becomes FL_ROUND_DOWN_BOX, which has the same shape.
    // (FL_ROUNDED_BOX's pressed look is a square, whose corners stay
    // behind on release.)
    settings_btn->box(FL_ROUND_UP_BOX);
    // The button only borrows the image and never deletes it. It's needed
    // for as long as the window exists, so it's simply never freed.
    auto* gear = new Fl_SVG_Image(nullptr, ICON_SETTINGS);
    gear->scale(18, 18);
    settings_btn->image(gear);
    settings_btn->tooltip("Settings");
    settings_btn->clear_visible_focus();    // No dotted focus ring after a click
    settings_btn->callback(on_settings, this);

    // No resizable() on purpose: FLTK then keeps the window at a fixed size.
    win.end();
}

MainWindow::~MainWindow() = default;

void MainWindow::show(int argc, char** argv){
    win.show(argc, argv);
    clipboard_init();    // Needs the display, which show() opens
}

void MainWindow::on_search(Fl_Widget*, void* data){
    MainWindow* self = static_cast<MainWindow*>(data);

    std::string keyword = self->search_bar->text();
    if (keyword.find_first_not_of(" \t") == std::string::npos) return;

    const Api api = self->search_bar->api();

    // Checked before anything changes on screen: without a key there's no
    // search at all. Copied for the search thread, since the settings may
    // change while it runs.
    std::string deepl_key;
    if (api == Api::DeepL){
        deepl_key = effective_deepl_key(self->settings.get());
        if (deepl_key.empty()){
            // Modal: waits for OK before returning
            fl_alert("No DeepL API key found.\n\n"
                     "Input the DeepL key in the settings or "
                     "set the DEEPL_API_KEY environment variable to use DeepL.");
            return;
        }
    }

    self->move_bar_to_top();

    if (api == Api::MyMemory){
        self->show_text("MyMemory isn't connected yet.");
        return;
    }

    self->show_text(std::format("Searching for {}...", keyword));
    const unsigned id = ++self->latest_search;

    // The request can take seconds, and FLTK only redraws between
    // callbacks, so doing it here would freeze the window. The thread gets
    // copies of what it needs; it never touches a widget itself.
    std::thread([self, id, api, keyword = std::move(keyword), deepl_key = std::move(deepl_key)]{
        auto* result = new SearchResult{.win = self, .id = id, .keyword = keyword};
        try {
            if (api == Api::DeepL) result->translation = deepl_translate(keyword, deepl_key);
            else result->entries = jisho_search(keyword);
        } catch (const std::exception& e){
            result->error = e.what();
        }

        // Queues on_search_done to run on the UI thread, which then owns
        // result. Widgets may only be changed from there.
        Fl::awake(on_search_done, result);
    }).detach();    // Nothing waits for it; it ends on its own after awake()
}

void MainWindow::on_settings(Fl_Widget*, void* data){
    MainWindow* self = static_cast<MainWindow*>(data);
    if (!self->settings_win) self->settings_win = std::make_unique<SettingsWindow>(self->settings, self->downloader);
    self->settings_win->show();
}

void MainWindow::on_search_done(void* data){
    // unique_ptr so result is deleted when this returns
    std::unique_ptr<SearchResult> result(static_cast<SearchResult*>(data));
    result->win->show_result(*result);
}

// Like Google: the bar starts in the middle and jumps to the top on the
// first search, and the results box appears in the space under it.
void MainWindow::move_bar_to_top(){
    const int top = MARGIN;
    if (search_bar->y() == top) return;

    // Moving a group moves its children (input, buttons) along with it
    search_bar->position(search_bar->x(), top);
    const int results_y = top + search_bar->h() + MARGIN;
    results->resize(results->x(), results_y, results->w(), win.h() - results_y - MARGIN);
    results->show();
    win.redraw();
}

void MainWindow::show_result(const SearchResult& result){
    // A newer search started since this one; its result will come later
    if (result.id != latest_search) return;

    if (!result.error.empty())
        show_text(std::format("Search failed: {}", result.error));
    else if (result.translation)
        show_text(*result.translation);
    else if (result.entries.empty())
        show_text(std::format("No results for {}.", result.keyword));
    else
        show_text(format_entries(result.entries));
}

void MainWindow::show_text(std::string_view text){
    show_text(std::vector<TextSpan>{{std::string(text)}});
}

void MainWindow::show_text(const std::vector<TextSpan>& spans){
    results->scroll_to_top();
    results_text->text(spans);
    // Its scrollbar depends on the new text's height
    results->redraw();
}
