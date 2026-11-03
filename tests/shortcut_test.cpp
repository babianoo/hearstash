// Tests for reading and writing the capture shortcut. No framework: each
// failed check prints where it was, and the exit code is how many failed.
// Run with: ctest --test-dir build

#include <cstdio>
#include <optional>
#include <string>

#include "core/shortcut.h"

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)){ \
        std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

// text read and written again, or "-" if it can't be read
static std::string round_trip(const char* text){
    const std::optional<Shortcut> shortcut = parse_shortcut(text);
    return shortcut ? format_shortcut(*shortcut) : "-";
}

static void test_reads(){
    const std::optional<Shortcut> shortcut = parse_shortcut("Win+Alt+C");
    CHECK(shortcut && shortcut->win && shortcut->alt && !shortcut->ctrl && !shortcut->shift && shortcut->key == "C");
    CHECK(round_trip(DEFAULT_SHORTCUT) == DEFAULT_SHORTCUT);
}

static void test_case_spaces_and_order(){
    CHECK(round_trip("ctrl + shift + pageup") == "Ctrl+Shift+PageUp");
    CHECK(round_trip(" alt+ctrl+win+shift+z ") == "Win+Ctrl+Alt+Shift+Z");
    CHECK(round_trip("Ctrl+f12") == "Ctrl+F12");
    CHECK(round_trip("Alt+7") == "Alt+7");
    CHECK(round_trip("Win+space") == "Win+Space");
}

static void test_alone(){
    // F-keys and Pause type nothing, so they can go alone
    CHECK(round_trip("F9") == "F9");
    CHECK(round_trip("Shift+F24") == "Shift+F24");
    CHECK(round_trip("Pause") == "Pause");
    // These would take away a letter, a capital or the space bar
    CHECK(round_trip("A") == "-");
    CHECK(round_trip("Shift+A") == "-");
    CHECK(round_trip("Space") == "-");
    CHECK(shortcut_problem(Shortcut{.key = "A"}) == "Add Ctrl, Alt or Win to it");
    CHECK(shortcut_problem(Shortcut{.ctrl = true, .key = "A"}).empty());
}

static void test_rejects(){
    CHECK(round_trip("") == "-");
    CHECK(round_trip("Ctrl+") == "-");
    CHECK(round_trip("Ctrl+Alt") == "-");
    CHECK(round_trip("Ctrl+Ctrl+A") == "-");
    CHECK(round_trip("Hyper+A") == "-");
    CHECK(round_trip("Ctrl+F0") == "-");
    CHECK(round_trip("Ctrl+F25") == "-");
    CHECK(round_trip("Ctrl+F01") == "-");
    CHECK(round_trip("Ctrl+Escape") == "-");
    CHECK(round_trip("Ctrl+AB") == "-");
    CHECK(round_trip("Ctrl+A+B") == "-");
    CHECK(shortcut_key_name("=").empty());
}

int main(){
    test_reads();
    test_case_spaces_and_order();
    test_alone();
    test_rejects();
    if (failures == 0) std::puts("All shortcut tests passed");
    return failures;
}
