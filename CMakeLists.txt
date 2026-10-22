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
find_package(FLTK REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json 3.11 REQUIRED)
# System FLTK may be built with cairo support, so its public header needs
# cairo.h even though we don't use cairo directly.
pkg_check_modules(CAIRO QUIET cairo)

# Add new source files here, by path from the project root.
add_executable(prototype
    src/main.cpp
    src/core/http.cpp
    src/core/deepl.cpp
    src/core/jisho.cpp
    src/ui/main_window.cpp
    src/ui/search_bar.cpp
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
