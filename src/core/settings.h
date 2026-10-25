#pragma once

#include <filesystem>
#include <string>

// What the settings window changes and the rest of the app reads, saved
// as JSON. Grouped like the window's categories, each group an object in
// the file:
//
//   {"api": {"deepl_key": "..."}, "speech": {"whisper_model": "small"},
//    "version": 1}
//
// The field names are the JSON keys, so renaming one changes the file
// format. A field's default is what it gets when the file doesn't have it.

// The API category
struct ApiSettings {
    // Empty when there's none
    std::string deepl_key;
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

struct Settings {
    ApiSettings api;
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
