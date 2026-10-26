#pragma once

#include <filesystem>
#include <string_view>

// For a file the app couldn't read, e.g. one edited by hand into invalid
// JSON. Renames it to <file>.bad, so the user can fix it and the next save
// can't overwrite it, and prints why: error, and instead, what the app
// does without it ("the settings start from the defaults"). It has to be
// closed first: Windows can't rename an open file.
//
// Never throws: a file that can't be renamed stays where it is.
void set_aside_bad_file(const std::filesystem::path& file, std::string_view error,
                        std::string_view instead);
