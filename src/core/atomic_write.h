#pragma once

#include <filesystem>
#include <string_view>

// Replaces file with contents, creating its folder if needed. It's written
// next to file first, then moved over it in one step, so a crash while
// writing leaves the old file as it was, not half of the new one.
// Throws std::runtime_error or std::filesystem::filesystem_error.
void write_file_atomically(const std::filesystem::path& file, std::string_view contents);
