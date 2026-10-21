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
# System FLTK may be built with cairo support, so its public header needs
# cairo.h even though we don't use cairo directly.
pkg_check_modules(CAIRO QUIET cairo)

# Add new source files here, by path from the project root.
add_executable(prototype
    src/main.cpp
    src/ui/main_window.cpp
    src/ui/theme.cpp
)

# Includes are written from src/, e.g. #include "core/anki/anki_connect.h"
target_include_directories(prototype PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${FLTK_INCLUDE_DIR}
    ${CAIRO_INCLUDE_DIRS}
)
target_link_libraries(prototype PRIVATE ${FLTK_LIBRARIES})
