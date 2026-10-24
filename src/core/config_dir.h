#pragma once

#include <filesystem>

// The folder the app keeps its files in, e.g. settings.json:
//   Windows: %APPDATA%\JapaneseHelper
//   Linux:   $XDG_CONFIG_HOME/JapaneseHelper, or ~/.config/JapaneseHelper
// It may not exist yet: whatever writes a file into it creates it.
// Throws std::runtime_error if the variables it's built from aren't set.
std::filesystem::path config_dir();
