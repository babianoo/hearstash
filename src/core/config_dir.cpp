#include "core/config_dir.h"

#include <cstdlib>
#include <format>
#include <stdexcept>

// The same on every platform
static constexpr const char* APP_DIR = "Hearstash";
// What the app was called before, so its folders can be moved over
static constexpr const char* OLD_APP_DIR = "Prototype";

// The app's folder in parent, moving the old one there if only it exists.
// If the move fails the app just starts afresh.
static std::filesystem::path app_dir(const std::filesystem::path& parent){
    const std::filesystem::path dir = parent / APP_DIR;
    const std::filesystem::path old = parent / OLD_APP_DIR;
    std::error_code error;
    if (!std::filesystem::exists(dir, error) && std::filesystem::is_directory(old, error))
        std::filesystem::rename(old, dir, error);
    return dir;
}

#ifdef _WIN32
// The app's folder in the one var names. Read as wide characters: a
// non-ASCII user name in the path wouldn't survive the narrow getenv.
static std::filesystem::path app_dir_in(const wchar_t* var){
    const wchar_t* dir = _wgetenv(var);
    if (!dir || !*dir){
        std::filesystem::path name(var);
        throw std::runtime_error(std::format("{} isn't set", name.string()));
    }
    return app_dir(dir);
}
#else
// The XDG Base Directory spec: the app's folder in the one var names, or
// if it isn't set, in fallback under the home folder. The spec says to
// ignore a relative path in var too.
static std::filesystem::path app_dir_in(const char* var, const char* fallback){
    const char* xdg = std::getenv(var);
    if (xdg && *xdg == '/') return app_dir(xdg);
    const char* home = std::getenv("HOME");
    if (!home || !*home) throw std::runtime_error(std::format("Neither {} nor HOME is set", var));
    return app_dir(std::filesystem::path(home) / fallback);
}
#endif

std::filesystem::path config_dir(){
#ifdef _WIN32
    // Roaming AppData, since settings belong to the user rather than the
    // machine
    return app_dir_in(L"APPDATA");
#else
    return app_dir_in("XDG_CONFIG_HOME", ".config");
#endif
}

std::filesystem::path state_dir(){
#ifdef _WIN32
    // Windows has no folder for state, and Roaming follows the user from
    // machine to machine like the history should
    return app_dir_in(L"APPDATA");
#else
    return app_dir_in("XDG_STATE_HOME", ".local/state");
#endif
}

std::filesystem::path data_dir(){
#ifdef _WIN32
    // Local AppData, so gigabytes of models aren't copied from machine to
    // machine with a roaming profile
    return app_dir_in(L"LOCALAPPDATA");
#else
    return app_dir_in("XDG_DATA_HOME", ".local/share");
#endif
}
