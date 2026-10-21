#include "ui/main_window.h"

#include <format>
#include <stdexcept>
#include <string_view>
#include <thread>

// FLTK
#include <FL/Fl.H>
#include <FL/Fl_Text_Display.H>

#include "ui/search_bar.h"

// Plain-text version of the results, like japanese-helper's output box:
//
//   兄弟 (きょうだい)
//       1. brothers and sisters; siblings  [Noun]
static std::string format_entries(const std::vector<JishoEntry>& entries){
    std::string text;
    for (const JishoEntry& entry : entries){
        for (const JishoForm& form : entry.forms){
            if (form.word.empty()) text += form.reading + "  ";
            else text += std::format("{} ({})  ", form.word, form.reading);
        }
        text += '\n';

        int n = 1;
        for (const JishoSense& sense : entry.senses){
            text += std::format("    {}. ", n++);
            for (size_t i = 0; i < sense.definitions.size(); ++i){
                if (i > 0) text += "; ";
                text += sense.definitions[i];
            }
            if (!sense.parts_of_speech.empty()){
                text += "  [";
                for (size_t i = 0; i < sense.parts_of_speech.size(); ++i){
                    if (i > 0) text += ", ";
                    text += sense.parts_of_speech[i];
                }
                text += ']';
            }
            text += '\n';
        }
        text += '\n';
    }
    return text;
}

MainWindow::MainWindow(){
    // win is constructed (as a member) before this body runs, so it's
    // the current group here: widgets created below are added to it.

    // Centered, a bit above the middle
    const int bar_w = 600;
    const int bar_x = (win.w() - bar_w) / 2, bar_y = win.h() * 2 / 5;
    search_bar = new SearchBar(bar_x, bar_y, bar_w);
    search_bar->callback(on_search, this);

    // Fills the space under the bar, same width
    const int results_y = bar_y + search_bar->h() + 20;
    results = new Fl_Text_Display(bar_x, results_y, bar_w, win.h() - results_y - 20);
    results->buffer(&results_buf);
    results->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);

    // No resizable() on purpose: FLTK then keeps the window at a fixed size.
    win.end();
}

void MainWindow::show(int argc, char** argv){
    win.show(argc, argv);
}

void MainWindow::on_search(Fl_Widget*, void* data){
    MainWindow* self = static_cast<MainWindow*>(data);

    std::string keyword = self->search_bar->text();
    if (keyword.find_first_not_of(" \t") == std::string::npos) return;

    if (self->search_bar->api() != Api::Jisho){
        self->results_buf.text("Only Jisho is connected so far.");
        return;
    }

    self->results_buf.text(std::format("Searching for {}...", keyword).c_str());
    const unsigned id = ++self->latest_search;

    // The request can take seconds, and FLTK only redraws between
    // callbacks, so doing it here would freeze the window. The thread gets
    // copies of what it needs; it never touches a widget itself.
    std::thread([self, id, keyword = std::move(keyword)]{
        auto* result = new SearchResult{.win = self, .id = id, .keyword = keyword};
        try {
            result->entries = jisho_search(keyword);
        } catch (const std::exception& e){
            result->error = e.what();
        }

        // Queues on_search_done to run on the UI thread, which then owns
        // result. Widgets may only be changed from there.
        Fl::awake(on_search_done, result);
    }).detach();    // Nothing waits for it; it ends on its own after awake()
}

void MainWindow::on_search_done(void* data){
    // unique_ptr so result is deleted when this returns
    std::unique_ptr<SearchResult> result(static_cast<SearchResult*>(data));
    result->win->show_result(*result);
}

void MainWindow::show_result(const SearchResult& result){
    // A newer search started since this one; its result will come later
    if (result.id != latest_search) return;

    if (!result.error.empty())
        results_buf.text(std::format("Search failed: {}", result.error).c_str());
    else if (result.entries.empty())
        results_buf.text(std::format("No results for {}.", result.keyword).c_str());
    else
        results_buf.text(format_entries(result.entries).c_str());
}
