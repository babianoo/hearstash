#include "core/shortcut.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <vector>

// Whether a and b are the same, ignoring the case of ASCII letters
static bool same(std::string_view a, std::string_view b){
    return std::ranges::equal(a, b, [](char x, char y){
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

// The keys with names other than one letter, digit or F-number
static constexpr std::array<const char*, 12> NAMED_KEYS = {
    "Space", "Insert", "Delete", "Home", "End", "PageUp", "PageDown", "Up", "Down", "Left", "Right", "Pause",
};

// The number n of an F-key's name "Fn", from 1 to 24. 0 if it isn't one.
static int function_key(std::string_view key){
    if (key.size() < 2 || key.size() > 3 || (key[0] != 'F' && key[0] != 'f')) return 0;
    int n = 0;
    for (char c : key.substr(1)){
        if (!std::isdigit(static_cast<unsigned char>(c))) return 0;
        n = n * 10 + (c - '0');
    }
    // Not "F01"
    if (key[1] == '0' || n < 1 || n > 24) return 0;
    return n;
}

std::string shortcut_key_name(std::string_view key){
    if (key.size() == 1){
        const unsigned char c = static_cast<unsigned char>(key[0]);
        if (std::isalpha(c)) return std::string(1, static_cast<char>(std::toupper(c)));
        if (std::isdigit(c)) return std::string(key);
        return "";
    }
    if (const int n = function_key(key)) return std::format("F{}", n);
    for (const char* name : NAMED_KEYS)
        if (same(key, name)) return name;
    return "";
}

std::string shortcut_problem(const Shortcut& shortcut){
    if (shortcut_key_name(shortcut.key).empty()) return "It needs a key other than Ctrl, Alt, Shift and Win";
    if (shortcut.ctrl || shortcut.alt || shortcut.win) return "";
    if (function_key(shortcut.key) || shortcut.key == "Pause") return "";
    return "Add Ctrl, Alt or Win to it";
}

// text without the spaces around it
static std::string_view trim(std::string_view text){
    const size_t start = text.find_first_not_of(' ');
    if (start == std::string_view::npos) return {};
    return text.substr(start, text.find_last_not_of(' ') - start + 1);
}

std::optional<Shortcut> parse_shortcut(std::string_view text){
    std::vector<std::string_view> parts;
    for (size_t start = 0;;){
        const size_t plus = text.find('+', start);
        parts.push_back(trim(text.substr(start, plus == std::string_view::npos ? std::string_view::npos : plus - start)));
        if (plus == std::string_view::npos) break;
        start = plus + 1;
    }

    Shortcut shortcut;
    // Every part but the last is a modifier, each at most once
    for (size_t i = 0; i + 1 < parts.size(); ++i){
        bool* modifier = same(parts[i], "Win")   ? &shortcut.win
                       : same(parts[i], "Ctrl")  ? &shortcut.ctrl
                       : same(parts[i], "Alt")   ? &shortcut.alt
                       : same(parts[i], "Shift") ? &shortcut.shift
                       : nullptr;
        if (!modifier || *modifier) return std::nullopt;
        *modifier = true;
    }
    shortcut.key = shortcut_key_name(parts.back());
    if (!shortcut_problem(shortcut).empty()) return std::nullopt;
    return shortcut;
}

std::string format_shortcut(const Shortcut& shortcut){
    std::string text;
    if (shortcut.win) text += "Win+";
    if (shortcut.ctrl) text += "Ctrl+";
    if (shortcut.alt) text += "Alt+";
    if (shortcut.shift) text += "Shift+";
    return text + shortcut.key;
}
