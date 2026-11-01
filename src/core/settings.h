#pragma once

#include <filesystem>
#include <string>

// What the settings window changes and the rest of the app reads, saved
// as JSON. Grouped like the window's categories, each group an object in
// the file:
//
//   {"anki": {"deck": "Mining"}, "api": {"deepl_key": "..."},
//    "buffer": {"seconds": 30}, "history": {"capacity": 100},
//    "speech": {"whisper_model": "small"}, "version": 1}
//
// The field names are the JSON keys, so renaming one changes the file
// format. A field's default is what it gets when the file doesn't have it.

// The API category
struct ApiSettings {
    // Empty when there's none
    std::string deepl_key;
};

// The History category
struct HistorySettings {
    // How many searches the history keeps: the newest ones. Between
    // MIN_CAPACITY and MAX_CAPACITY.
    int capacity = 100;

    static constexpr int MIN_CAPACITY = 10;
    static constexpr int MAX_CAPACITY = 1000;
};

// The Speech category
struct SpeechSettings {
    // The id of the whisper model to transcribe with, e.g. "small", from
    // WHISPER_MODELS. Empty when none is chosen. It may not be downloaded
    // (yet, or any more).
    std::string whisper_model;
    // The name of the microphone to record from, as list_input_devices()
    // gives it. Empty for the system's default one, which is also used
    // when this one isn't plugged in.
    std::string input_device;
};

// The Buffer category: the rolling buffer of what the computer plays
struct BufferSettings {
    // Whether it records at all. Off, nothing is kept.
    bool enabled = true;
    // How much it keeps, the newest. Between MIN_SECONDS and MAX_SECONDS.
    int seconds = 30;
    // How much of the end of a capture is selected when it opens. Between
    // 1 and seconds.
    int selection_seconds = 8;

    static constexpr int MIN_SECONDS = 10;
    static constexpr int MAX_SECONDS = 120;
};

// The Anki category: where captures are added as cards
struct AnkiSettings {
    // As Anki names them. Empty when none is chosen.
    std::string deck;
    std::string note_type;
    // The fields of note_type that get each part of a card. Empty for one
    // that goes nowhere.
    std::string word_field;
    std::string reading_field;
    std::string meaning_field;
    std::string sentence_field;
    std::string audio_field;
    std::string picture_field;
    // The picture's JPEG quality, from MIN_QUALITY to MAX_QUALITY: higher
    // is sharper, but takes more space in the collection
    int picture_quality = 85;
    // The last crop of a screenshot, as fractions of its width and height,
    // so it still fits after the resolution changes. crop_w == 0 when
    // there's none yet. The video is usually in the same place next time.
    double crop_x = 0, crop_y = 0, crop_w = 0, crop_h = 0;

    static constexpr int MIN_QUALITY = 1;
    static constexpr int MAX_QUALITY = 100;
};

struct Settings {
    AnkiSettings anki;
    ApiSettings api;
    BufferSettings buffer;
    HistorySettings history;
    SpeechSettings speech;
};

// Never throws: without a file, e.g. on the first run, the settings are
// all defaults. So they are with an unreadable one, which is renamed to
// <file>.bad first, so the user can fix it and the next save can't
// overwrite it.
Settings load_settings(const std::filesystem::path& file);

// Creates file's folder if needed. The old file is only replaced once the
// new one is completely written, so a crash mid-save can't leave half a
// file. Throws std::runtime_error or std::filesystem::filesystem_error.
void save_settings(const std::filesystem::path& file, const Settings& settings);

// Fixes values the rest of the app shouldn't have to check, e.g. the spaces
// a pasted key may come with. load_settings calls it, and so should
// whatever changes the settings.
void sanitize(Settings& settings);

// The DeepL key to search with: the saved one, or if there's none, the
// DEEPL_API_KEY environment variable, which is never saved. Empty when
// neither is set.
std::string effective_deepl_key(const Settings& settings);
