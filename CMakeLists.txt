cmake_minimum_required(VERSION 3.21...4.3.1)
# C as well, for whisper.cpp's ggml
project(hearstash C CXX)

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
# For saving the pictures on Anki cards. libjpeg-turbo is usually there
# already, since FLTK loads JPEGs with it.
find_package(JPEG REQUIRED)
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
add_executable(hearstash
    src/main.cpp
    src/core/anki_connect.cpp
    src/core/anki_fields.cpp
    src/core/atomic_write.cpp
    src/core/audio_buffer.cpp
    src/core/bad_file.cpp
    src/core/card_format.cpp
    src/core/clip_player.cpp
    src/core/hearstash_note_type.cpp
    src/core/history.cpp
    src/core/http.cpp
    src/core/image.cpp
    src/core/deepl.cpp
    src/core/jisho.cpp
    src/core/local_time.cpp
    src/core/lookup_text.cpp
    src/core/media_encoding.cpp
    src/core/config_dir.cpp
    src/core/recorder.cpp
    src/core/resample.cpp
    src/core/sample_ring.cpp
    src/core/screenshot.cpp
    src/core/settings.cpp
    src/core/sha1.cpp
    src/core/shortcut.cpp
    src/core/time_ranges.cpp
    src/core/transcriber.cpp
    src/core/whisper_models.cpp
    src/ui/bring_to_front.cpp
    src/ui/capture_window.cpp
    src/ui/card_window.cpp
    src/ui/capturer.cpp
    src/ui/category_button.cpp
    src/ui/clipboard.cpp
    src/ui/clipboard_input.cpp
    src/ui/crop_view.cpp
    src/ui/filter_bar.cpp
    src/ui/global_hotkey.cpp
    src/ui/history_store.cpp
    src/ui/history_table.cpp
    src/ui/history_window.cpp
    src/ui/jisho_format.cpp
    src/ui/main_window.cpp
    src/ui/model_downloader.cpp
    src/ui/search_bar.cpp
    src/ui/settings_store.cpp
    src/ui/settings_window.cpp
    src/ui/shared_transcriber.cpp
    src/ui/shortcut_input.cpp
    src/ui/smooth_scroll.cpp
    src/ui/theme.cpp
    src/ui/waveform.cpp
    src/ui/wrapped_text.cpp
)
# A GUI program on Windows, so starting it (or the capture shortcut running
# it with --capture) doesn't open a console window. main() stays the entry
# point with MinGW.
set_target_properties(hearstash PROPERTIES WIN32_EXECUTABLE ON)
# The app icon, built into the .exe
if(WIN32)
    target_sources(hearstash PRIVATE resources/hearstash.rc)
endif()
# Only on our own code: whisper.cpp sets its own warnings, and miniaudio's
# aren't ours to fix
target_compile_options(hearstash PRIVATE -Wall -Wextra)

# Includes are written from src/, e.g. #include "core/anki/anki_connect.h"
target_include_directories(hearstash PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${FLTK_INCLUDE_DIR}
    ${CAIRO_INCLUDE_DIRS}
)
target_link_libraries(hearstash PRIVATE
    ${FLTK_LIBRARIES}
    CURL::libcurl
    JPEG::JPEG
    nlohmann_json::nlohmann_json
    # Brings its include directory and ggml's libraries with it
    whisper
    miniaudio
)
if(WAYLAND_CLIENT_FOUND)
    target_link_libraries(hearstash PRIVATE PkgConfig::WAYLAND_CLIENT)
    target_compile_definitions(hearstash PRIVATE HAVE_WAYLAND_CLIENT)
endif()

# --- Tests: ctest --test-dir build ---
# Plain executables for the code that doesn't need a device or a window
enable_testing()
add_executable(sample_ring_test tests/sample_ring_test.cpp src/core/sample_ring.cpp)
target_compile_options(sample_ring_test PRIVATE -Wall -Wextra)
target_include_directories(sample_ring_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME sample_ring COMMAND sample_ring_test)

add_executable(resample_test tests/resample_test.cpp src/core/resample.cpp)
target_compile_options(resample_test PRIVATE -Wall -Wextra)
target_include_directories(resample_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(resample_test PRIVATE miniaudio)
add_test(NAME resample COMMAND resample_test)

add_executable(lookup_text_test tests/lookup_text_test.cpp src/core/lookup_text.cpp)
target_compile_options(lookup_text_test PRIVATE -Wall -Wextra)
target_include_directories(lookup_text_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME lookup_text COMMAND lookup_text_test)

add_executable(image_test tests/image_test.cpp src/core/image.cpp src/core/screenshot.cpp)
target_compile_options(image_test PRIVATE -Wall -Wextra)
target_include_directories(image_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(image_test PRIVATE JPEG::JPEG nlohmann_json::nlohmann_json)
# For screenshot.cpp's GDI capture, which the test doesn't run
if(WIN32)
    target_link_libraries(image_test PRIVATE gdi32 user32)
endif()
add_test(NAME image COMMAND image_test)

add_executable(media_encoding_test tests/media_encoding_test.cpp src/core/media_encoding.cpp)
target_compile_options(media_encoding_test PRIVATE -Wall -Wextra)
target_include_directories(media_encoding_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME media_encoding COMMAND media_encoding_test)

add_executable(anki_fields_test tests/anki_fields_test.cpp src/core/anki_fields.cpp)
target_compile_options(anki_fields_test PRIVATE -Wall -Wextra)
target_include_directories(anki_fields_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME anki_fields COMMAND anki_fields_test)

add_executable(card_format_test tests/card_format_test.cpp src/core/card_format.cpp src/core/anki_fields.cpp)
target_compile_options(card_format_test PRIVATE -Wall -Wextra)
target_include_directories(card_format_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME card_format COMMAND card_format_test)

add_executable(shortcut_test tests/shortcut_test.cpp src/core/shortcut.cpp)
target_compile_options(shortcut_test PRIVATE -Wall -Wextra)
target_include_directories(shortcut_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME shortcut COMMAND shortcut_test)
