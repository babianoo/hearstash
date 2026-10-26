cmake_minimum_required(VERSION 3.21...4.3.1)
# C as well, for whisper.cpp's ggml
project(prototype C CXX)

# Require C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
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

# --- Dependencies built with the app (git submodules in external/) ---
# Fetch them with: git submodule update --init
# whisper.cpp, for speech to text. Linked statically, so there are no
# whisper or ggml DLLs to ship next to the executable. Its tests and
# examples are already off when it's built as part of another project.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build whisper.cpp and ggml as static libraries")
# ggml uses its own thread pool instead. With MinGW, CMake finds OpenMP's
# import library rather than libgomp.a, so even a -static build would
# need libgomp-1.dll next to it.
set(GGML_OPENMP OFF CACHE BOOL "Use OpenMP in ggml")
add_subdirectory(external/whisper.cpp EXCLUDE_FROM_ALL)

# miniaudio, for recording from the microphone. It's one header, compiled
# once by its miniaudio.c. Its own CMakeLists.txt also builds decoders,
# effect nodes and install rules, so it's built here instead, with only
# device I/O. The definitions are PUBLIC: code that includes miniaudio.h
# has to see it as it was compiled.
add_library(miniaudio STATIC external/miniaudio/miniaudio.c)
target_include_directories(miniaudio PUBLIC external/miniaudio)
target_compile_definitions(miniaudio PUBLIC
    MA_NO_DECODING
    MA_NO_ENCODING
    MA_NO_GENERATION
    MA_NO_RESOURCE_MANAGER
    MA_NO_NODE_GRAPH
    MA_NO_ENGINE
)
# It loads the audio backends (WASAPI, PulseAudio, ALSA...) when it runs,
# so nothing else is needed to build it. On Linux that takes libdl, and
# its device threads take pthreads.
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

# Add new source files here, by path from the project root.
add_executable(prototype
    src/main.cpp
    src/core/atomic_write.cpp
    src/core/bad_file.cpp
    src/core/history.cpp
    src/core/http.cpp
    src/core/deepl.cpp
    src/core/jisho.cpp
    src/core/config_dir.cpp
    src/core/recorder.cpp
    src/core/settings.cpp
    src/core/sha1.cpp
    src/core/transcriber.cpp
    src/core/whisper_models.cpp
    src/ui/category_button.cpp
    src/ui/clipboard.cpp
    src/ui/clipboard_input.cpp
    src/ui/main_window.cpp
    src/ui/model_downloader.cpp
    src/ui/search_bar.cpp
    src/ui/settings_store.cpp
    src/ui/settings_window.cpp
    src/ui/smooth_scroll.cpp
    src/ui/theme.cpp
    src/ui/wrapped_text.cpp
)
# Only on our own code: whisper.cpp sets its own warnings, and miniaudio's
# aren't ours to fix
target_compile_options(prototype PRIVATE -Wall -Wextra)

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
    # Brings its include directory and ggml's libraries with it
    whisper
    miniaudio
)
if(WAYLAND_CLIENT_FOUND)
    target_link_libraries(prototype PRIVATE PkgConfig::WAYLAND_CLIENT)
    target_compile_definitions(prototype PRIVATE HAVE_WAYLAND_CLIENT)
endif()
