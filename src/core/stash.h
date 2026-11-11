#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/image.h"

// Lines stashed to look up later: captures that are saved instead of
// opened, for learners who don't want to stop the show (PLAN.md, phase 9).
// Each is a folder in the stash folder, named after when it was taken:
//
//   stash/20261111-210433/
//     audio.wav        the whole buffer, 16-bit mono
//     screenshot.jpg   if there was one
//     thumbnail.jpg    a small copy of it, for the list
//     stash.json       {"time": 1793012345, "app": "mpv", "rate": 48000,
//                       "selection_seconds": 8, "screenshot_error": "",
//                       "transcript": "...", "transcript_error": "",
//                       "version": 1}
//
// The transcript is of the end the capture window selects at first, and
// is only there if the settings say to transcribe stashed lines right
// away: it's added once whisper is done, after the rest is saved.
//
// stash.json is written last, so a folder without it was cut off while
// saving and is ignored. Only the user can read the files (mode 0600 on
// Linux), since they're recordings of what they watched.

// How tall the thumbnail is, in pixels
inline constexpr int STASH_THUMBNAIL_HEIGHT = 90;

// A capture to stash
struct StashCapture {
    // The whole buffer, rate a second
    std::span<const float> audio;
    int rate = 0;
    // How much of its end the capture window selects at first
    int selection_seconds = 0;
    // Empty if it couldn't be taken; screenshot_error then says why
    const Image* screenshot = nullptr;
    std::string screenshot_error;
    // The program that had the focus (focused_app()), "" if unknown
    std::string app;
    // When, as Unix time
    std::int64_t time = 0;
};

// One stashed line, as the list shows it. Its audio and screenshot are
// only read by load_stashed.
struct StashEntry {
    // Its folder's name
    std::string id;
    std::int64_t time = 0;
    std::string app;
    int rate = 0;
    int selection_seconds = 0;
    std::string screenshot_error;
    // What whisper made of the selection, if it was transcribed when
    // stashed. Empty if it wasn't (yet), or found no speech;
    // transcript_error then says why it couldn't be, if it couldn't.
    std::string transcript;
    std::string transcript_error;
    // How long the audio is
    double seconds = 0;
    // What its files take on disk
    std::uintmax_t bytes = 0;
};

// A stashed line in full, to open in the capture window
struct StashedLine {
    std::vector<float> audio;
    int rate = 0;
    int selection_seconds = 0;
    // Empty if there was none; screenshot_error then says why
    Image screenshot;
    std::string screenshot_error;
    // Of the selection; empty if there's none
    std::string transcript;
};

// Saves capture in a new folder in dir, creating dir if needed, and
// returns it as the list will show it. Throws std::runtime_error or
// std::filesystem::filesystem_error; a folder it started is removed then.
StashEntry stash_line(const std::filesystem::path& dir, const StashCapture& capture);

// The lines in dir, newest first. Never throws: without dir there are
// none, and a line that can't be read is skipped and printed.
std::vector<StashEntry> list_stash(const std::filesystem::path& dir);

// The audio and screenshot of entry, from dir. Throws std::runtime_error
// if they can't be read, e.g. deleted meanwhile. A broken screenshot only
// fills screenshot_error, since the audio is what matters.
StashedLine load_stashed(const std::filesystem::path& dir, const StashEntry& entry);

// Saves the transcript of the line with id, or why there's none. Throws
// std::runtime_error if the line is gone, e.g. deleted while it was
// being transcribed, or std::filesystem::filesystem_error.
void set_stash_transcript(const std::filesystem::path& dir, std::string_view id, std::string_view transcript,
                          std::string_view error);

// entry's thumbnail, or an empty Image if it has none or it can't be read
Image load_stash_thumbnail(const std::filesystem::path& dir, const StashEntry& entry);

// Deletes the line with id, if there's one. Throws
// std::filesystem::filesystem_error if it can't be.
void delete_stashed(const std::filesystem::path& dir, std::string_view id);

// Deletes every line, and anything half-written. Throws
// std::filesystem::filesystem_error if it can't.
void clear_stash(const std::filesystem::path& dir);
