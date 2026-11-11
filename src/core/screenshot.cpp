#include "core/screenshot.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <format>
#include <filesystem>
#include <iterator>
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

std::string focused_app(){
    const HWND window = GetForegroundWindow();
    if (!window) return "";
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return "";
    wchar_t path[1024];
    DWORD size = static_cast<DWORD>(std::size(path));
    const BOOL ok = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    if (!ok) return "";

    // The program's file name, e.g. "fate.exe", in UTF-8 and lowercase
    const std::wstring name = std::filesystem::path(std::wstring(path, size)).filename().wstring();
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(std::max(bytes, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), utf8.data(), bytes, nullptr, nullptr);
    std::ranges::transform(utf8, utf8.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return utf8;
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
// Decoding the screenshot portal's PNG files
#include <png.h>

#ifdef HAVE_SD_BUS
#include <chrono>
#include <iostream>
#include <memory>
#include <string_view>

#include <poll.h>

// D-Bus, for the screenshot portal
#include <systemd/sd-bus.h>
#endif

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

std::string focused_app(){
    if (!std::getenv("HYPRLAND_INSTANCE_SIGNATURE")) return "";
    try {
        // A Wine or Proton game's class is its .exe's name or steam_app_<id>
        const std::vector<std::uint8_t> output = run({"hyprctl", "-j", "activewindow"});
        return nlohmann::json::parse(output).value("class", "");
    } catch (const std::exception&){
        return "";
    }
}

Rect monitor_in_screenshot(std::string_view layout_json, std::string_view output, int image_w, int image_h){
    struct Monitor { std::string name; double x, y, w, h; };
    std::vector<Monitor> monitors;
    try {
        for (const nlohmann::json& o : nlohmann::json::parse(layout_json).at("outputs")){
            if (!o.value("enabled", false)) continue;
            const nlohmann::json& pos = o.at("pos");
            const nlohmann::json& size = o.at("size");
            double w = size.at("width").get<double>(), h = size.at("height").get<double>();
            // KScreen's rotations: 1 none, 2 left, 4 upside down, 8 right
            const int rotation = o.value("rotation", 1);
            if (rotation == 2 || rotation == 8) std::swap(w, h);
            // Its position is in logical pixels, its size in real ones
            const double scale = o.value("scale", 1.0) > 0 ? o.value("scale", 1.0) : 1.0;
            monitors.push_back({o.value("name", ""), pos.at("x").get<double>(), pos.at("y").get<double>(),
                                w / scale, h / scale});
        }
    } catch (const nlohmann::json::exception&){
        return {};
    }

    const auto found = std::ranges::find(monitors, output, &Monitor::name);
    if (found == monitors.end() || image_w <= 0 || image_h <= 0) return {};

    // The box around them all is the whole screenshot, maybe scaled
    double left = found->x, top = found->y, right = found->x + found->w, bottom = found->y + found->h;
    for (const Monitor& m : monitors){
        left = std::min(left, m.x);
        top = std::min(top, m.y);
        right = std::max(right, m.x + m.w);
        bottom = std::max(bottom, m.y + m.h);
    }
    const double sx = image_w / (right - left), sy = image_h / (bottom - top);
    // E.g. monitors at different scales, which the portal may put together
    // otherwise: better all of them than the wrong part
    if (std::abs(sx - sy) > 0.02 * std::max(sx, sy)) return {};

    const int x0 = static_cast<int>(std::lround((found->x - left) * sx));
    const int y0 = static_cast<int>(std::lround((found->y - top) * sy));
    const int x1 = static_cast<int>(std::lround((found->x + found->w - left) * sx));
    const int y1 = static_cast<int>(std::lround((found->y + found->h - top) * sy));
    return {.x = x0, .y = y0, .w = std::min(x1, image_w) - x0, .h = std::min(y1, image_h) - y0};
}

Image parse_png(const std::vector<std::uint8_t>& data){
    // libpng's simplified API: it converts whatever the file holds (16-bit,
    // a palette, alpha) to 8-bit RGB
    png_image png{};
    png.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&png, data.data(), data.size()))
        throw std::runtime_error(std::format("The screenshot isn't a PNG image: {}", png.message));
    png.format = PNG_FORMAT_RGB;

    Image image{.width = static_cast<int>(png.width), .height = static_cast<int>(png.height), .rgb = {}};
    image.rgb.resize(PNG_IMAGE_SIZE(png));
    if (!png_image_finish_read(&png, nullptr, image.rgb.data(), 0, nullptr)){
        const std::string message = png.message;
        png_image_free(&png);
        throw std::runtime_error(std::format("Couldn't read the screenshot's PNG image: {}", message));
    }
    return image;
}

Image qt_pixels_to_image(std::span<const std::uint8_t> pixels, int width, int height, int stride, unsigned format){
    // Where red is in a pixel's 4 bytes; green is always the second, and
    // blue is where red isn't. QImage::Format's numbers:
    size_t red;
    switch (format){
        // RGB32, ARGB32, ARGB32_Premultiplied: 0xAARRGGBB as a number, so
        // blue comes first in memory (on a little-endian machine)
        case 4: case 5: case 6: red = 2; break;
        // RGBX8888, RGBA8888, RGBA8888_Premultiplied: bytes in that order
        case 16: case 17: case 18: red = 0; break;
        default: throw std::runtime_error(std::format("the screenshot is in an unknown pixel format ({})", format));
    }
    if (width <= 0 || height <= 0 || stride / 4 < width
        || pixels.size() / static_cast<size_t>(stride) < static_cast<size_t>(height))
        throw std::runtime_error("the screenshot's pixels are cut short");

    Image image{.width = width, .height = height, .rgb = {}};
    image.rgb.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
    std::uint8_t* to = image.rgb.data();
    for (int y = 0; y < height; ++y){
        const std::uint8_t* from = pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(stride);
        for (int x = 0; x < width; ++x, from += 4, to += 3){
            to[0] = from[red];
            to[1] = from[1];
            to[2] = from[2 - red];
        }
    }
    return image;
}

// A screenshot from grim, of the focused monitor on Hyprland, or all of
// them. Throws std::runtime_error if grim isn't installed or can't, as on
// any desktop but a wlroots one.
static Image grim_screenshot(){
    // PPM: grim doesn't spend time compressing it, and it's simple to read
    std::vector<std::string> args{"grim", "-t", "ppm"};
    if (const std::string monitor = hyprland_focused_monitor(); !monitor.empty()){
        args.push_back("-o");
        args.push_back(monitor);
    }
    args.push_back("-");    // To stdout
    return parse_ppm(run(args));
}

#ifdef HAVE_SD_BUS

namespace {
// sd-bus objects, released when they go out of scope
struct BusDeleter { void operator()(sd_bus* bus) const { sd_bus_flush_close_unref(bus); } };
struct SlotDeleter { void operator()(sd_bus_slot* slot) const { sd_bus_slot_unref(slot); } };
struct MessageDeleter { void operator()(sd_bus_message* message) const { sd_bus_message_unref(message); } };
using Bus = std::unique_ptr<sd_bus, BusDeleter>;
using Slot = std::unique_ptr<sd_bus_slot, SlotDeleter>;
using Message = std::unique_ptr<sd_bus_message, MessageDeleter>;

struct BusError {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    ~BusError(){ sd_bus_error_free(&error); }
};

// What the portal's Response signal said
struct Response {
    bool arrived = false;
    std::uint32_t code = 2;
    std::string uri;
    int error = 0;    // A negative errno if the signal couldn't be read
};
}

// What KWin says of the picture it then writes to the pipe
namespace {
struct KWinPicture {
    std::string type;
    std::uint32_t width = 0, height = 0, stride = 0, format = 0;
};

// A file descriptor, closed when it goes out of scope
struct Fd {
    int fd = -1;
    ~Fd(){ reset(); }
    void reset(){
        if (fd >= 0) close(fd);
        fd = -1;
    }
};
}

// Reads the a{sv} KWin answers a capture with. Negative errno if it can't.
static int read_kwin_picture(sd_bus_message* message, KWinPicture& picture){
    int r = sd_bus_message_enter_container(message, 'a', "{sv}");
    while (r > 0 && (r = sd_bus_message_enter_container(message, 'e', "sv")) > 0){
        const char* key = nullptr;
        const char* contents = nullptr;
        r = sd_bus_message_read(message, "s", &key);
        if (r >= 0) r = sd_bus_message_peek_type(message, nullptr, &contents);
        if (r < 0) break;
        const std::string_view name = key, type = contents ? contents : "";
        std::uint32_t* number = name == "width" ? &picture.width : name == "height" ? &picture.height
                              : name == "stride" ? &picture.stride : name == "format" ? &picture.format : nullptr;
        if (number && type == "u") r = sd_bus_message_read(message, "v", "u", number);
        else if (name == "type" && type == "s"){
            const char* text = nullptr;
            r = sd_bus_message_read(message, "v", "s", &text);
            if (r >= 0) picture.type = text;
        }
        else r = sd_bus_message_skip(message, "v");
        if (r >= 0) r = sd_bus_message_exit_container(message);
    }
    return r;
}

// A screenshot of the monitor with the focus straight from KWin
// (org.kde.KWin.ScreenShot2), which is what the portal asks too on KDE.
// KWin writes the pixels to a pipe as they are, so nothing is compressed,
// saved and read back: a few hundredths of a second where the portal
// takes 2 s. Without the mouse pointer.
//
// KWin decides who may: it's meant for programs whose .desktop file asks
// for it (X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2).
// Throws std::runtime_error if it says no, or this isn't KDE; denied then
// says whether KWin was there to say it.
static Image kwin_screenshot(bool& denied){
    const auto fail = [](const char* what, int r){
        return std::runtime_error(std::format("{}: {}", what, std::strerror(-r)));
    };
    denied = false;

    sd_bus* opened = nullptr;
    if (const int r = sd_bus_open_user(&opened); r < 0) throw fail("couldn't connect to D-Bus", r);
    const Bus bus(opened);

    int ends[2];
    if (pipe2(ends, O_CLOEXEC) != 0) throw fail("couldn't make a pipe", -errno);
    Fd from{ends[0]}, to{ends[1]};
    // A 4K screen is 33 MB: fewer, bigger writes than 64 KB at a time. If
    // it's refused, the pipe just stays as it was.
    fcntl(to.fd, F_SETPIPE_SZ, 1 << 20);

    // In the monitor's real pixels, not scaled to the desktop's
    BusError error;
    sd_bus_message* replied = nullptr;
    if (sd_bus_call_method(bus.get(), "org.kde.KWin", "/org/kde/KWin/ScreenShot2", "org.kde.KWin.ScreenShot2",
                           "CaptureActiveScreen", &error.error, &replied,
                           "a{sv}h", 1, "native-resolution", "b", 1, to.fd) < 0){
        // No KWin on the bus is another desktop, not a no
        denied = !sd_bus_error_has_name(&error.error, SD_BUS_ERROR_SERVICE_UNKNOWN)
              && !sd_bus_error_has_name(&error.error, SD_BUS_ERROR_NAME_HAS_NO_OWNER);
        throw std::runtime_error(std::format("KWin didn't take a screenshot: {}",
                                             error.error.message ? error.error.message : error.error.name));
    }
    const Message reply(replied);
    // KWin has its own copy of the write end: with ours closed, the pipe
    // ends when KWin is done, or gone
    to.reset();

    KWinPicture picture;
    if (const int r = read_kwin_picture(reply.get(), picture); r < 0) throw fail("couldn't read KWin's answer", r);
    // 16384 x 16384 at most: more isn't a screen
    const size_t size = static_cast<size_t>(picture.stride) * picture.height;
    if (picture.type != "raw" || picture.width == 0 || picture.height == 0 || picture.stride / 4 < picture.width
        || picture.width > 16384 || picture.height > 16384 || picture.stride > 4 * 16384)
        throw std::runtime_error("KWin's screenshot isn't one this can read");

    std::vector<std::uint8_t> pixels(size);
    size_t got = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (got < size){
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        pollfd waiting{.fd = from.fd, .events = POLLIN, .revents = 0};
        const int ready = left.count() > 0 ? poll(&waiting, 1, static_cast<int>(left.count())) : 0;
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) throw std::runtime_error("KWin didn't send the screenshot");
        const ssize_t n = read(from.fd, pixels.data() + got, size - got);
        if (n > 0) got += static_cast<size_t>(n);
        else if (n == 0) throw std::runtime_error("KWin's screenshot is cut short");
        else if (errno != EINTR) throw fail("couldn't read KWin's screenshot", -errno);
    }
    return qt_pixels_to_image(pixels, static_cast<int>(picture.width), static_cast<int>(picture.height),
                              static_cast<int>(picture.stride), picture.format);
}

// sd-bus calls this with the Request's Response: a code (0 done, 1 the
// user said no, 2 anything else) and results, of which "uri" is the file
static int on_response(sd_bus_message* message, void* data, sd_bus_error*){
    Response& response = *static_cast<Response*>(data);
    response.arrived = true;
    int r = sd_bus_message_read(message, "u", &response.code);
    if (r >= 0) r = sd_bus_message_enter_container(message, 'a', "{sv}");
    while (r > 0 && (r = sd_bus_message_enter_container(message, 'e', "sv")) > 0){
        const char* key = nullptr;
        r = sd_bus_message_read(message, "s", &key);
        if (r >= 0 && std::strcmp(key, "uri") == 0){
            const char* uri = nullptr;
            r = sd_bus_message_read(message, "v", "s", &uri);
            if (r >= 0) response.uri = uri;
        }
        else if (r >= 0) r = sd_bus_message_skip(message, "v");
        if (r >= 0) r = sd_bus_message_exit_container(message);
    }
    if (r < 0) response.error = r;
    return 0;
}

// The path of a file:// URI, with its %XX escapes decoded. Empty if it
// isn't one.
static std::string file_uri_path(const std::string& uri){
    constexpr std::string_view scheme = "file://";
    if (!uri.starts_with(scheme)) return "";
    std::string path;
    for (size_t i = scheme.size(); i < uri.size(); ++i){
        if (uri[i] == '%' && i + 2 < uri.size() && std::isxdigit(uri[i + 1]) && std::isxdigit(uri[i + 2])){
            path += static_cast<char>(std::stoi(uri.substr(i + 1, 2), nullptr, 16));
            i += 2;
        }
        else path += uri[i];
    }
    return path;
}

static std::vector<std::uint8_t> read_file(const std::string& path){
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error(std::format("couldn't open {}: {}", path, std::strerror(errno)));
    std::vector<std::uint8_t> data;
    std::uint8_t chunk[1 << 16];
    for (;;){
        const ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n > 0) data.insert(data.end(), chunk, chunk + n);
        else if (n == 0) break;
        else if (errno != EINTR){
            const int error = errno;
            close(fd);
            throw std::runtime_error(std::format("couldn't read {}: {}", path, std::strerror(error)));
        }
    }
    close(fd);
    return data;
}

// A screenshot of all the monitors from the XDG Screenshot portal
// (org.freedesktop.portal.Screenshot), which KDE, GNOME and Hyprland
// have. It answers through a Request object's Response signal; the portal
// saves the picture as a PNG file (KDE and GNOME put it in ~/Pictures),
// which is read, then deleted, since only the app wanted it. Throws
// std::runtime_error if there's no portal, the user says no, or it doesn't
// answer within a minute (the first time it may ask the user to allow it).
static Image portal_screenshot(){
    const auto fail = [](const char* what, int r){
        return std::runtime_error(std::format("{}: {}", what, std::strerror(-r)));
    };

    sd_bus* opened = nullptr;
    if (const int r = sd_bus_open_user(&opened); r < 0) throw fail("couldn't connect to D-Bus", r);
    const Bus bus(opened);

    // The portal makes the Request at a path it builds from our name on the
    // bus and a token we choose, so its Response can be listened for before
    // asking, and can't be missed
    const char* unique_name = nullptr;
    if (const int r = sd_bus_get_unique_name(bus.get(), &unique_name); r < 0) throw fail("couldn't connect to D-Bus", r);
    std::string sender = unique_name + 1;    // Without the ":"
    std::ranges::replace(sender, '.', '_');
    static unsigned count = 0;
    const std::string token = std::format("hearstash{}_{}", getpid(), ++count);
    const std::string request = std::format("/org/freedesktop/portal/desktop/request/{}/{}", sender, token);

    Response response;
    sd_bus_slot* matched = nullptr;
    if (const int r = sd_bus_match_signal(bus.get(), &matched, "org.freedesktop.portal.Desktop", request.c_str(),
                                          "org.freedesktop.portal.Request", "Response", on_response, &response); r < 0)
        throw fail("couldn't listen to the screenshot portal", r);
    const Slot slot(matched);

    // Not interactive: no dialog to pick an area, just the whole desktop.
    // No parent window, since the capture window isn't open yet.
    BusError error;
    sd_bus_message* replied = nullptr;
    if (sd_bus_call_method(bus.get(), "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                           "org.freedesktop.portal.Screenshot", "Screenshot", &error.error, &replied,
                           "sa{sv}", "", 2, "handle_token", "s", token.c_str(), "interactive", "b", 0) < 0)
        throw std::runtime_error(std::format("the screenshot portal failed: {}",
                                             error.error.message ? error.error.message : error.error.name));
    const Message reply(replied);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    while (!response.arrived){
        const int r = sd_bus_process(bus.get(), nullptr);
        if (r < 0) throw fail("lost the screenshot portal", r);
        if (r > 0) continue;    // Maybe more to do right away
        const auto left = deadline - std::chrono::steady_clock::now();
        if (left <= std::chrono::steady_clock::duration::zero()) throw std::runtime_error("the screenshot portal didn't answer");
        if (const int w = sd_bus_wait(bus.get(), std::chrono::duration_cast<std::chrono::microseconds>(left).count()); w < 0 && w != -EINTR)
            throw fail("lost the screenshot portal", w);
    }
    if (response.error < 0) throw fail("couldn't read the screenshot portal's answer", response.error);
    if (response.code == 1) throw std::runtime_error("screenshots weren't allowed");
    if (response.code != 0) throw std::runtime_error("the screenshot portal failed");

    const std::string path = file_uri_path(response.uri);
    if (path.empty()) throw std::runtime_error(std::format("the screenshot portal gave an unknown place: {}", response.uri));
    std::vector<std::uint8_t> png;
    try {
        png = read_file(path);
    } catch (...){
        unlink(path.c_str());
        throw;
    }
    unlink(path.c_str());
    return parse_png(png);
}

// KWin's name for the monitor with the focus, e.g. "DP-1". Empty when not
// on KDE, or it can't tell.
static std::string kde_active_output(){
    sd_bus* opened = nullptr;
    if (sd_bus_open_user(&opened) < 0) return "";
    const Bus bus(opened);
    BusError error;
    sd_bus_message* reply = nullptr;
    if (sd_bus_call_method(bus.get(), "org.kde.KWin", "/KWin", "org.kde.KWin", "activeOutputName",
                           &error.error, &reply, "") < 0)
        return "";
    const Message owned(reply);
    const char* name = nullptr;
    if (sd_bus_message_read(reply, "s", &name) < 0 || !name) return "";
    return name;
}

// The part of all, a screenshot of every monitor, that's the monitor
// named output, as kscreen-doctor lays them out. all as it is if that
// can't be told.
static Image only_monitor(Image all, const std::string& output){
    if (output.empty()) return all;
    try {
        const std::vector<std::uint8_t> layout = run({"kscreen-doctor", "-j"});
        const Rect monitor = monitor_in_screenshot(std::string_view(reinterpret_cast<const char*>(layout.data()), layout.size()),
                                                   output, all.width, all.height);
        if (!monitor.empty()) return crop(all, monitor);
    } catch (const std::exception&){
        // Not installed, say
    }
    return all;
}

#endif

Image take_screenshot(){
    std::string grim_error;
    try {
        return grim_screenshot();
    } catch (const std::runtime_error& e){
        grim_error = e.what();
    }
#ifdef HAVE_SD_BUS
    try {
        bool denied = false;
        try {
            return kwin_screenshot(denied);
        } catch (const std::runtime_error& e){
            // The portal, then. Said once if KWin was there and said no,
            // since that's the difference between a capture that opens at
            // once and one that takes 2 s.
            static bool said = false;
            if (denied && !said)
                std::cerr << std::format("{}. Asking the screenshot portal instead, which is slower.\n", e.what());
            said = said || denied;
        }
        // Asked first: the portal's dialog, when it shows one, could take
        // the focus
        const std::string output = kde_active_output();
        return only_monitor(portal_screenshot(), output);
    } catch (const std::runtime_error& e){
        throw std::runtime_error(std::format(
            "Couldn't take a screenshot: {}, and {}. Screenshots need the desktop's screenshot portal "
            "(xdg-desktop-portal), or grim on a wlroots desktop like Hyprland or Sway.",
            grim_error, e.what()));
    }
#else
    throw std::runtime_error(std::format(
        "Couldn't take a screenshot: {}. This build has no screenshot portal (it was built without libsystemd), "
        "so screenshots need grim, on a wlroots desktop like Hyprland or Sway.",
        grim_error));
#endif
}

#endif
