#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/jisho.h"

// Only pointers to these are stored here, so the full headers are
// included in lookup_panels.cpp instead.
class SmoothScroll;
class WrappedText;
struct CardDraft;
struct Settings;
struct TextSpan;

// The bottom half of the capture windows: the text on the left, big, and
// a panel on the right. Selecting part of the text with the mouse looks it
// up on Jisho, in the panel; translate() shows the whole text's DeepL
// translation there instead. The audio capture window's text is whisper's
// transcript, the screen capture window's what was read on the screen.
//
// Not a widget: it adds its widgets to the window being built, and the
// window deletes them. UI thread only, like them.
class LookupPanels {
public:
    // Adds the widgets to the current group, filling x, y, w, h
    LookupPanels(int x, int y, int w, int h);

    // Its threads hold a pointer to it. One per window.
    LookupPanels(const LookupPanels&) = delete;
    LookupPanels& operator=(const LookupPanels&) = delete;

    // Shows text, with a hint to select a word in the panel
    void show_text(const std::string& text);
    // Shows note small and grey in the text's place, e.g. what to do or
    // what's happening, and empties the panel. There's no text then.
    void show_note(const std::string& note);
    // Empties both
    void clear(){ show_note(""); }

    // The text shown, empty while a note is
    const std::string& text() const { return text_; }

    // Translates the text with DeepL into the panel, with the key settings
    // has, or says in a dialog that there's none. Does nothing without
    // text.
    void translate(const Settings& settings);

    // A card's text: the sentence, and the word looked up last with what
    // Jisho said of it
    void fill(CardDraft& draft) const;

    // Called when text() changes
    void on_change(std::function<void()> listener){ change_listener = std::move(listener); }

private:
    // What a lookup thread hands back to the UI thread
    struct LookupResult {
        LookupPanels* panels;
        unsigned id;
        std::string keyword;
        std::vector<JishoEntry> entries{};
        std::string error{};    // Non-empty if it failed
    };
    // What a translation thread hands back to the UI thread
    struct TranslationResult {
        LookupPanels* panels;
        unsigned id;
        std::string translation{};
        std::string error{};    // Non-empty if it failed
    };

    // Created with new in the constructor and added to the current group,
    // which deletes them. Each WrappedText is its SmoothScroll's only
    // child.
    SmoothScroll* text_scroll = nullptr;
    WrappedText* text_view = nullptr;
    SmoothScroll* lookup_scroll = nullptr;
    WrappedText* lookup_view = nullptr;

    std::string text_;
    // The first entry of the lookup shown, if any, and what was looked up
    std::optional<JishoEntry> entry;
    std::string keyword;
    // Bumped on every lookup and translation, which share the panel, and
    // when the text changes. Only the latest one's result is shown.
    unsigned latest = 0;

    std::function<void()> change_listener;

    // Run on the UI thread, called by Fl::awake() from a lookup or
    // translation thread
    static void on_lookup_done(void* data);
    static void on_translation_done(void* data);

    void set_text(const std::vector<TextSpan>& spans);
    void set_panel(const std::vector<TextSpan>& spans);
    // Looks up what's selected in the text, if it's worth it
    void look_up_selection();
};
