#include "core/screenshot.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <stdexcept>
#include <string>

Image parse_ppm(const std::vector<std::uint8_t>& data){
    // The header is "P6", then the width, height and largest sample value,
    // each after whitespace and maybe "#" comments, then one whitespace
    // byte and the pixels
    size_t at = 0;
    const auto skip_space = [&]{
        while (at < data.size()){
            if (data[at] == '#') while (at < data.size() && data[at] != '\n') ++at;
            else if (std::isspace(data[at])) ++at;
            else break;
        }
    };
    const auto number = [&]{
        skip_space();
        long value = 0;
        const size_t start = at;
        while (at < data.size() && std::isdigit(data[at]) && value < 1'000'000) value = value * 10 + (data[at++] - '0');
        if (at == start) throw std::runtime_error("The screenshot isn't a PPM image");
        return value;
    };

    if (data.size() < 2 || data[0] != 'P' || data[1] != '6') throw std::runtime_error("The screenshot isn't a PPM image");
    at = 2;
    const long width = number(), height = number(), max = number();
    if (max != 255) throw std::runtime_error("The screenshot's PPM image isn't 8-bit");
    ++at;    // The one whitespace byte before the pixels

    const size_t size = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
    if (width <= 0 || height <= 0 || data.size() - std::min(at, data.size()) < size)
        throw std::runtime_error("The screenshot's PPM image is cut short");

    Image image{.width = static_cast<int>(width), .height = static_cast<int>(height), .rgb = {}};
    image.rgb.assign(data.begin() + static_cast<std::ptrdiff_t>(at), data.begin() + static_cast<std::ptrdiff_t>(at + size));
    return image;
}

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
// Already defined by MinGW's C++ library, so std::min isn't broken
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// The GDI objects a screenshot needs, released in reverse when it goes out
// of scope, however far it got
namespace {
struct Gdi {
    HDC screen = nullptr;
    HDC memory = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;

    ~Gdi(){
        if (previous) SelectObject(memory, previous);
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        if (screen) ReleaseDC(nullptr, screen);
    }
};
}

Image take_screenshot(){
    // The monitor with the foreground window, or the main one when nothing
    // has the focus. In real pixels: FLTK made the app DPI-aware when its
    // first window opened.
    const HMONITOR monitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) throw std::runtime_error("Couldn't take a screenshot: no monitor found");
    const RECT area = info.rcMonitor;
    const int width = area.right - area.left, height = area.bottom - area.top;

    // A bitmap whose pixels can be read straight from memory: 4 bytes each
    // (blue, green, red, unused), rows from the top
    BITMAPINFO format{};
    format.bmiHeader.biSize = sizeof(format.bmiHeader);
    format.bmiHeader.biWidth = width;
    format.bmiHeader.biHeight = -height;
    format.bmiHeader.biPlanes = 1;
    format.bmiHeader.biBitCount = 32;
    format.bmiHeader.biCompression = BI_RGB;

    Gdi gdi;
    void* pixels = nullptr;
    gdi.screen = GetDC(nullptr);
    if (gdi.screen) gdi.memory = CreateCompatibleDC(gdi.screen);
    if (gdi.memory) gdi.bitmap = CreateDIBSection(gdi.screen, &format, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!gdi.bitmap || !pixels) throw std::runtime_error("Couldn't take a screenshot: out of memory for it");
    gdi.previous = SelectObject(gdi.memory, gdi.bitmap);

    // The desktop's coordinates start at the main monitor's corner, so
    // another monitor's can be negative. CAPTUREBLT takes layered windows
    // too, which some video players are.
    if (!BitBlt(gdi.memory, 0, 0, width, height, gdi.screen, area.left, area.top, SRCCOPY | CAPTUREBLT))
        throw std::runtime_error(std::format("Couldn't take a screenshot: error {}", GetLastError()));
    GdiFlush();

    Image image{.width = width, .height = height, .rgb = {}};
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    image.rgb.resize(count * 3);
    const auto* from = static_cast<const std::uint8_t*>(pixels);
    for (size_t i = 0; i < count; ++i){
        image.rgb[i * 3] = from[i * 4 + 2];
        image.rgb[i * 3 + 1] = from[i * 4 + 1];
        image.rgb[i * 3 + 2] = from[i * 4];
    }
    return image;
}

#else

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

// JSON
#include <nlohmann/json.hpp>

extern char** environ;

// Runs program with args, without a shell, and returns what it wrote to
// stdout. Throws std::runtime_error if it can't be run or fails.
static std::vector<std::uint8_t> run(const std::vector<std::string>& args){
    int out[2];
    if (pipe2(out, O_CLOEXEC) != 0) throw std::runtime_error(std::format("Couldn't run {}: {}", args[0], std::strerror(errno)));

    // Its stdout is the pipe's write end; the read end stays ours
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);

    std::vector<char*> argv;
    for (const std::string& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    pid_t pid;
    const int error = posix_spawnp(&pid, args[0].c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(out[1]);
    if (error != 0){
        close(out[0]);
        if (error == ENOENT) throw std::runtime_error(std::format("{} isn't installed", args[0]));
        throw std::runtime_error(std::format("Couldn't run {}: {}", args[0], std::strerror(error)));
    }

    std::vector<std::uint8_t> output;
    std::uint8_t chunk[1 << 16];
    for (;;){
        const ssize_t n = read(out[0], chunk, sizeof(chunk));
        if (n > 0) output.insert(output.end(), chunk, chunk + n);
        else if (n == 0 || errno != EINTR) break;
    }
    close(out[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR){}
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) throw std::runtime_error(std::format("{} failed", args[0]));
    return output;
}

// Hyprland's name for the monitor with the focus, e.g. "eDP-1". Empty
// when not on Hyprland, or it can't tell.
static std::string hyprland_focused_monitor(){
    if (!std::getenv("HYPRLAND_INSTANCE_SIGNATURE")) return "";
    try {
        const std::vector<std::uint8_t> output = run({"hyprctl", "-j", "monitors"});
        for (const nlohmann::json& monitor : nlohmann::json::parse(output))
            if (monitor.value("focused", false)) return monitor.value("name", "");
    } catch (const std::exception&){
        // All of them, then
    }
    return "";
}

Image take_screenshot(){
    // PPM: grim doesn't spend time compressing it, and it's simple to read
    std::vector<std::string> args{"grim", "-t", "ppm"};
    if (const std::string monitor = hyprland_focused_monitor(); !monitor.empty()){
        args.push_back("-o");
        args.push_back(monitor);
    }
    args.push_back("-");    // To stdout

    try {
        return parse_ppm(run(args));
    } catch (const std::runtime_error& e){
        throw std::runtime_error(std::format(
            "Couldn't take a screenshot: {}. Screenshots need grim, on a wlroots desktop like Hyprland or Sway.",
            e.what()));
    }
}

#endif
