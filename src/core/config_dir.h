#pragma once

#include <filesystem>

// Where the app keeps its files. Neither folder may exist yet: whatever
// writes a file into one creates it. Both throw std::runtime_error if the
// variables they're built from aren't set.

// For small files that belong to the user, e.g. settings.json:
//   Windows: %APPDATA%\Prototype
//   Linux:   $XDG_CONFIG_HOME/Prototype, or ~/.config/Prototype
std::filesystem::path config_dir();

// For large files the app can download again, e.g. whisper models:
//   Windows: %LOCALAPPDATA%\Prototype
//   Linux:   $XDG_DATA_HOME/Prototype, or ~/.local/share/Prototype
std::filesystem::path data_dir();
