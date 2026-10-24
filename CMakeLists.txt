cmake_minimum_required(VERSION 3.21...4.3.1)
project(prototype CXX)

# Require C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)
# Write build/compile_commands.json so clangd knows the include paths
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies (must be installed on the system) ---
find_package(PkgConfig REQUIRED)
# Prefer FLTK's own CMake package (1.4+): its targets also link FLTK's
# dependencies (bundled zlib/png/jpeg, gdiplus on Windows). Fall back to
# CMake's FindFLTK module when FLTK was installed without it.
find_package(FLTK CONFIG QUIET)
if(NOT FLTK_FOUND)
    find_package(FLTK REQUIRED)
endif()
find_package(CURL REQUIRED)
find_package(nlohmann_json 3.11 REQUIRED)
# System FLTK may be built with cairo support, so its public header needs
# cairo.h even though we don't use cairo directly.
pkg_check_modules(CAIRO QUIET cairo)
# For src/ui/clipboard.cpp, which works around FLTK's Wayland clipboard.
# Without it (an X11-only system) the app uses FLTK's clipboard as is.
pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)

# Add new source files here, by path from the project root.
add_executable(prototype
    src/main.cpp
    src/core/http.cpp
    src/core/deepl.cpp
    src/core/jisho.cpp
    src/core/config_dir.cpp
    src/ui/clipboard.cpp
    src/ui/clipboard_input.cpp
    src/ui/main_window.cpp
    src/ui/search_bar.cpp
    src/ui/settings_window.cpp
    src/ui/smooth_scroll.cpp
    src/ui/theme.cpp
    src/ui/wrapped_text.cpp
)

# Includes are written from src/, e.g. #include "core/anki/anki_connect.h"
target_include_directories(prototype PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${FLTK_INCLUDE_DIR}
    ${CAIRO_INCLUDE_DIRS}
)
target_link_libraries(prototype PRIVATE
    ${FLTK_LIBRARIES}
    CURL::libcurl
    nlohmann_json::nlohmann_json
)
if(WAYLAND_CLIENT_FOUND)
    target_link_libraries(prototype PRIVATE PkgConfig::WAYLAND_CLIENT)
    target_compile_definitions(prototype PRIVATE HAVE_WAYLAND_CLIENT)
endif()
