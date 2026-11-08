#pragma once

#include <optional>
#include <string>
#include <string_view>

// A key combination, for the capture shortcut on Windows. In the settings
// it's written like Windows writes them, e.g. "Win+Alt+C".
struct Shortcut {
    bool win = false;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    // The key pressed with them, by the name it's written with: "A" to "Z",
    // "0" to "9", "F1" to "F24", "Space", "Insert", "Delete", "Home",
    // "End", "PageUp", "PageDown", "Up", "Down", "Left", "Right" or "Pause"
    std::string key;

    bool operator==(const Shortcut&) const = default;
};

// The one a new install starts with. On Windows 11 nothing else uses it,
// and it has no AltGr in it, which types letters like ć in some layouts.
inline constexpr const char* DEFAULT_SHORTCUT = "Win+Alt+C";
// The same for the screen capture, next to it
inline constexpr const char* DEFAULT_SCREEN_SHORTCUT = "Win+Alt+X";

// Why shortcut can't be one, e.g. "Add Ctrl, Alt or Win to it". Empty if
// it can. A shortcut takes its keys from every app, so a key that types
// something needs Ctrl, Alt or Win with it (Shift isn't enough: Shift+A is
// a capital A). F1 to F24 and Pause can go without.
std::string shortcut_problem(const Shortcut& shortcut);

// key's name as Shortcut::key has it, whatever its case, e.g. "pageup" is
// "PageUp". Empty if it isn't one of those keys.
std::string shortcut_key_name(std::string_view key);

// Reads text like "Win+Alt+C", whatever the case and with any spaces
// around the parts. Empty if it isn't a key with modifiers in front, or
// shortcut_problem() has one.
std::optional<Shortcut> parse_shortcut(std::string_view text);

// shortcut written the way parse_shortcut reads it, the modifiers in the
// order Windows writes them: "Win+Ctrl+Alt+Shift+C"
std::string format_shortcut(const Shortcut& shortcut);
