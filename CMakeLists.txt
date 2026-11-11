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
# For the dictionary: its zip is read with zlib, and the app's own
# dictionary file is compressed with it. Usually there already, since curl
# and libpng use it.
find_package(ZLIB REQUIRED)
# System FLTK may be built with cairo support, so its public header needs
# cairo.h even though we don't use cairo directly.
pkg_check_modules(CAIRO QUIET cairo)
# For src/ui/clipboard.cpp, which works around FLTK's Wayland clipboard.
# Without it (an X11-only system) the app uses FLTK's clipboard as is.
pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
if(NOT WIN32)
    # For screenshots from the desktop's screenshot portal, which saves them
    # as PNG. libpng is usually there already, since FLTK loads PNGs with it.
    find_package(PNG REQUIRED)
    # sd-bus, to talk to the portal over D-Bus. Without it the app only
    # takes screenshots with grim, so only on wlroots desktops.
    pkg_check_modules(LIBSYSTEMD QUIET IMPORTED_TARGET libsystemd)
    if(NOT LIBSYSTEMD_FOUND)
        message(STATUS "libsystemd not found: screenshots will only work with grim, on wlroots desktops")
    endif()
endif()

# onnxruntime, to read text in screenshots (OCR) with PaddleOCR's models.
# Arch: onnxruntime-cpu. Elsewhere, e.g. Windows, Microsoft's prebuilt
# package from https://github.com/microsoft/onnxruntime/releases: pass its
# folder as -DONNXRUNTIME_ROOT=... Without it the app builds, but can't
# read text from the screen.
find_package(onnxruntime CONFIG QUIET)
if(NOT onnxruntime_FOUND)
    set(ONNXRUNTIME_ROOT "" CACHE PATH "Folder of Microsoft's prebuilt onnxruntime package")
    find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
        HINTS ${ONNXRUNTIME_ROOT}/include PATH_SUFFIXES onnxruntime)
    find_library(ONNXRUNTIME_LIBRARY onnxruntime HINTS ${ONNXRUNTIME_ROOT}/lib)
    if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
        add_library(onnxruntime::onnxruntime UNKNOWN IMPORTED)
        set_target_properties(onnxruntime::onnxruntime PROPERTIES
            IMPORTED_LOCATION ${ONNXRUNTIME_LIBRARY}
            INTERFACE_INCLUDE_DIRECTORIES ${ONNXRUNTIME_INCLUDE_DIR})
        set(onnxruntime_FOUND TRUE)
    else()
        message(STATUS "onnxruntime not found: the app won't read text from the screen")
    endif()
endif()

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
# Vulkan, to run whisper on the GPU: NVIDIA, AMD and Intel alike. Building
# it takes the Vulkan headers and loader, glslc (to compile ggml's
# shaders) and SPIRV-Headers; Arch: vulkan-headers, vulkan-icd-loader,
# shaderc, spirv-headers. HEARSTASH_GPU is AUTO (with Vulkan when those
# are found), ON (fail without them) or OFF (CPU only). It sets ggml's own
# GGML_VULKAN, which is overwritten.
# With it, the app needs the Vulkan loader (libvulkan.so.1, vulkan-1.dll)
# to start, which GPU drivers come with. Without a GPU that can take the
# model, it transcribes on the CPU.
set(HEARSTASH_GPU AUTO CACHE STRING "Run whisper on the GPU through Vulkan: AUTO, ON or OFF")
set_property(CACHE HEARSTASH_GPU PROPERTY STRINGS AUTO ON OFF)
set(VULKAN_BUILDABLE OFF)
if(NOT HEARSTASH_GPU STREQUAL "OFF")
    find_package(Vulkan QUIET COMPONENTS glslc)
    find_package(SPIRV-Headers CONFIG QUIET)
    if(Vulkan_FOUND AND Vulkan_glslc_FOUND AND SPIRV-Headers_FOUND)
        set(VULKAN_BUILDABLE ON)
    endif()
endif()
if(HEARSTASH_GPU STREQUAL "AUTO")
    set(USE_VULKAN ${VULKAN_BUILDABLE})
elseif(HEARSTASH_GPU)
    if(NOT VULKAN_BUILDABLE)
        message(FATAL_ERROR "HEARSTASH_GPU is ON, but the Vulkan headers and loader, glslc or SPIRV-Headers weren't found")
    endif()
    set(USE_VULKAN ON)
else()
    set(USE_VULKAN OFF)
endif()
if(NOT USE_VULKAN)
    message(STATUS "Building without Vulkan: whisper will only run on the CPU")
endif()
set(GGML_VULKAN ${USE_VULKAN} CACHE BOOL "Set from HEARSTASH_GPU" FORCE)
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
    src/core/deinflect.cpp
    src/core/dictionary.cpp
    src/core/dictionary_files.cpp
    src/core/dictionary_import.cpp
    src/core/jisho.cpp
    src/core/local_time.cpp
    src/core/lookup_text.cpp
    src/core/media_encoding.cpp
    src/core/ocr_models.cpp
    src/core/ocr_text.cpp
    src/core/text_reader.cpp
    src/core/verified_download.cpp
    src/core/config_dir.cpp
    src/core/recorder.cpp
    src/core/resample.cpp
    src/core/sample_ring.cpp
    src/core/screenshot.cpp
    src/core/settings.cpp
    src/core/sha1.cpp
    src/core/shortcut.cpp
    src/core/stash.cpp
    src/core/structured_content.cpp
    src/core/time_ranges.cpp
    src/core/transcriber.cpp
    src/core/whisper_models.cpp
    src/core/word_lookup.cpp
    src/core/zip_archive.cpp
    src/ui/bring_to_front.cpp
    src/ui/capture_window.cpp
    src/ui/card_window.cpp
    src/ui/capturer.cpp
    src/ui/category_button.cpp
    src/ui/clipboard.cpp
    src/ui/clipboard_input.cpp
    src/ui/crop_view.cpp
    src/ui/dictionary_updater.cpp
    src/ui/filter_bar.cpp
    src/ui/fit_line.cpp
    src/ui/global_hotkey.cpp
    src/ui/history_store.cpp
    src/ui/history_table.cpp
    src/ui/history_window.cpp
    src/ui/jisho_format.cpp
    src/ui/lookup_panels.cpp
    src/ui/main_window.cpp
    src/ui/model_downloader.cpp
    src/ui/screen_window.cpp
    src/ui/search_bar.cpp
    src/ui/settings_store.cpp
    src/ui/settings_window.cpp
    src/ui/shared_transcriber.cpp
    src/ui/shortcut_input.cpp
    src/ui/smooth_scroll.cpp
    src/ui/stash_store.cpp
    src/ui/stash_transcriber.cpp
    src/ui/stash_window.cpp
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
    ZLIB::ZLIB
    nlohmann_json::nlohmann_json
    # Brings its include directory and ggml's libraries with it
    whisper
    miniaudio
)
if(WAYLAND_CLIENT_FOUND)
    target_link_libraries(hearstash PRIVATE PkgConfig::WAYLAND_CLIENT)
    target_compile_definitions(hearstash PRIVATE HAVE_WAYLAND_CLIENT)
endif()
if(NOT WIN32)
    target_link_libraries(hearstash PRIVATE PNG::PNG)
endif()
if(LIBSYSTEMD_FOUND)
    target_link_libraries(hearstash PRIVATE PkgConfig::LIBSYSTEMD)
    target_compile_definitions(hearstash PRIVATE HAVE_SD_BUS)
endif()
if(onnxruntime_FOUND)
    target_link_libraries(hearstash PRIVATE onnxruntime::onnxruntime)
    target_compile_definitions(hearstash PRIVATE HAVE_ONNXRUNTIME)

    # Reads screenshots (PNG) with the models and prints their text, to
    # check the OCR on saved screenshots: tools/read_text.cpp says how
    add_executable(read_text tools/read_text.cpp src/core/text_reader.cpp src/core/ocr_text.cpp
                   src/core/image.cpp src/core/screenshot.cpp)
    target_compile_options(read_text PRIVATE -Wall -Wextra)
    target_include_directories(read_text PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(read_text PRIVATE HAVE_ONNXRUNTIME)
    target_link_libraries(read_text PRIVATE onnxruntime::onnxruntime JPEG::JPEG nlohmann_json::nlohmann_json)
    if(WIN32)
        target_link_libraries(read_text PRIVATE gdi32 user32)
    else()
        target_link_libraries(read_text PRIVATE PNG::PNG)
    endif()
endif()

# --- The dictionary the app ships with ---
# Jitendex (jitendex.org, CC BY-SA 4.0), converted to the app's own
# dictionary file and put next to the program, where the app looks for it.
# The zip is fetched when CMake configures, and again once the copy here
# is a week old, since a build to hand out should have a current one: the
# licence of the data Jitendex is built on asks for that. Without a
# connection the build goes on without a dictionary, and the app then looks
# words up on jisho.org until it has downloaded one itself.
#   -DHEARSTASH_DICTIONARY=OFF        builds no dictionary file
#   -DHEARSTASH_DICTIONARY_ZIP=<zip>  converts that zip instead of fetching one
option(HEARSTASH_DICTIONARY "Build the dictionary file the app ships with" ON)
set(HEARSTASH_DICTIONARY_ZIP "" CACHE FILEPATH "A Yomitan-format dictionary zip to convert instead of fetching Jitendex")
set(DICTIONARY_URL "https://github.com/stephenmk/stephenmk.github.io/releases/latest/download/jitendex-yomitan.zip")

# What the dictionary takes without the app around it: the converter here,
# and the tests further down
set(DICTIONARY_SOURCES
    src/core/dictionary.cpp
    src/core/dictionary_import.cpp
    src/core/http.cpp
    src/core/sha1.cpp
    src/core/verified_download.cpp
    src/core/zip_archive.cpp
)
# Converts a dictionary's zip: tools/make_dictionary.cpp says how
add_executable(make_dictionary tools/make_dictionary.cpp ${DICTIONARY_SOURCES})
target_compile_options(make_dictionary PRIVATE -Wall -Wextra)
target_include_directories(make_dictionary PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(make_dictionary PRIVATE ZLIB::ZLIB CURL::libcurl nlohmann_json::nlohmann_json)

if(HEARSTASH_DICTIONARY)
    set(DICTIONARY_ZIP "${HEARSTASH_DICTIONARY_ZIP}")
    if(NOT DICTIONARY_ZIP)
        set(DICTIONARY_ZIP "${CMAKE_BINARY_DIR}/jitendex-yomitan.zip")
        set(DICTIONARY_FETCH TRUE)
        if(EXISTS "${DICTIONARY_ZIP}")
            file(TIMESTAMP "${DICTIONARY_ZIP}" DICTIONARY_FETCHED "%s" UTC)
            string(TIMESTAMP DICTIONARY_NOW "%s" UTC)
            math(EXPR DICTIONARY_ZIP_AGE "${DICTIONARY_NOW} - ${DICTIONARY_FETCHED}")
            if(DICTIONARY_ZIP_AGE LESS 604800)
                set(DICTIONARY_FETCH FALSE)
            endif()
        endif()
        if(DICTIONARY_FETCH)
            message(STATUS "Fetching the dictionary (about 40 MB): ${DICTIONARY_URL}")
            # To the side first, so a download that fails leaves the copy
            # there was
            file(DOWNLOAD "${DICTIONARY_URL}" "${DICTIONARY_ZIP}.part" STATUS DICTIONARY_STATUS TLS_VERIFY ON)
            list(GET DICTIONARY_STATUS 0 DICTIONARY_ERROR)
            if(DICTIONARY_ERROR EQUAL 0)
                file(RENAME "${DICTIONARY_ZIP}.part" "${DICTIONARY_ZIP}")
            else()
                file(REMOVE "${DICTIONARY_ZIP}.part")
                list(GET DICTIONARY_STATUS 1 DICTIONARY_ERROR_TEXT)
                message(WARNING "Couldn't fetch the dictionary (${DICTIONARY_ERROR_TEXT})")
            endif()
        endif()
    endif()

    if(EXISTS "${DICTIONARY_ZIP}")
        # Next to the program. Also made again when the converter changes,
        # as the file's layout may have.
        set(DICTIONARY_FILE "${CMAKE_BINARY_DIR}/jitendex.hsdict")
        add_custom_command(
            OUTPUT "${DICTIONARY_FILE}"
            COMMAND make_dictionary "${DICTIONARY_ZIP}" "${DICTIONARY_FILE}"
            DEPENDS make_dictionary "${DICTIONARY_ZIP}"
            COMMENT "Converting the dictionary"
            VERBATIM)
        add_custom_target(dictionary ALL DEPENDS "${DICTIONARY_FILE}")
    else()
        message(WARNING "No dictionary zip: the app is built without a dictionary file")
    endif()
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
# For screenshot.cpp's GDI capture, which the test doesn't run, and its
# PNG decoding on Linux, which it does. It's built without HAVE_SD_BUS, so
# it doesn't need libsystemd.
if(WIN32)
    target_link_libraries(image_test PRIVATE gdi32 user32)
else()
    target_link_libraries(image_test PRIVATE PNG::PNG)
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

add_executable(ocr_text_test tests/ocr_text_test.cpp src/core/ocr_text.cpp)
target_compile_options(ocr_text_test PRIVATE -Wall -Wextra)
target_include_directories(ocr_text_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME ocr_text COMMAND ocr_text_test)

add_executable(shortcut_test tests/shortcut_test.cpp src/core/shortcut.cpp)
target_compile_options(shortcut_test PRIVATE -Wall -Wextra)
target_include_directories(shortcut_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME shortcut COMMAND shortcut_test)

add_executable(stash_test tests/stash_test.cpp src/core/stash.cpp src/core/image.cpp src/core/media_encoding.cpp
               src/core/atomic_write.cpp src/core/local_time.cpp)
target_compile_options(stash_test PRIVATE -Wall -Wextra)
target_include_directories(stash_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(stash_test PRIVATE JPEG::JPEG nlohmann_json::nlohmann_json)
add_test(NAME stash COMMAND stash_test)

add_executable(structured_content_test tests/structured_content_test.cpp src/core/structured_content.cpp)
target_compile_options(structured_content_test PRIVATE -Wall -Wextra)
target_include_directories(structured_content_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(structured_content_test PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME structured_content COMMAND structured_content_test)

add_executable(deinflect_test tests/deinflect_test.cpp src/core/deinflect.cpp)
target_compile_options(deinflect_test PRIVATE -Wall -Wextra)
target_include_directories(deinflect_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME deinflect COMMAND deinflect_test)

# With what looks words up in it too
add_executable(dictionary_test tests/dictionary_test.cpp ${DICTIONARY_SOURCES}
               src/core/anki_fields.cpp src/core/card_format.cpp src/core/config_dir.cpp src/core/deinflect.cpp
               src/core/dictionary_files.cpp src/core/jisho.cpp src/core/structured_content.cpp
               src/core/word_lookup.cpp)
target_compile_options(dictionary_test PRIVATE -Wall -Wextra)
target_include_directories(dictionary_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dictionary_test PRIVATE ZLIB::ZLIB CURL::libcurl nlohmann_json::nlohmann_json)
add_test(NAME dictionary COMMAND dictionary_test)
