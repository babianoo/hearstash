#include "core/config_dir.h"

#include <cstdlib>
#include <stdexcept>

// The same on every platform
static constexpr const char* APP_DIR = "JapaneseHelper";

std::filesystem::path config_dir(){
#ifdef _WIN32
    // Roaming AppData, since settings belong to the user rather than the
    // machine. Read as wide characters: a non-ASCII user name in the path
    // wouldn't survive the narrow getenv.
    const wchar_t* appdata = _wgetenv(L"APPDATA");
    if (!appdata || !*appdata) throw std::runtime_error("APPDATA isn't set");
    return std::filesystem::path(appdata) / APP_DIR;
#else
    // The XDG Base Directory spec, which falls back to ~/.config
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return std::filesystem::path(xdg) / APP_DIR;
    const char* home = std::getenv("HOME");
    if (!home || !*home) throw std::runtime_error("Neither XDG_CONFIG_HOME nor HOME is set");
    return std::filesystem::path(home) / ".config" / APP_DIR;
#endif
}
