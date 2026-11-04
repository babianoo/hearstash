#pragma once

#include <filesystem>

// Where the app keeps its files. None of the folders may exist yet:
// whatever writes a file into one creates it. All throw
// std::runtime_error if the variables they're built from aren't set.

// For small files that belong to the user, e.g. settings.json:
//   Windows: %APPDATA%\Hearstash
//   Linux:   $XDG_CONFIG_HOME/Hearstash, or ~/.config/Hearstash
std::filesystem::path config_dir();

// For what the app remembers between runs that isn't a setting, e.g. the
// search history:
//   Windows: %APPDATA%\Hearstash, the same as config_dir()
//   Linux:   $XDG_STATE_HOME/Hearstash, or ~/.local/state/Hearstash
std::filesystem::path state_dir();

// For large files the app can download again, e.g. whisper models:
//   Windows: %LOCALAPPDATA%\Hearstash
//   Linux:   $XDG_DATA_HOME/Hearstash, or ~/.local/share/Hearstash
std::filesystem::path data_dir();
