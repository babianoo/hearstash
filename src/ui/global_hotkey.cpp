#include "ui/global_hotkey.h"

#include <stdexcept>
#include <utility>

#ifdef _WIN32

#include <exception>
#include <format>
#include <iostream>
#include <optional>

#define WIN32_LEAN_AND_MEAN
// Already defined by MinGW's C++ library, so std::min isn't broken
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "core/shortcut.h"

// The hidden window's class, which send_hotkey_press finds it by
static constexpr const wchar_t* WINDOW_CLASS = L"PrototypeCaptureShortcut";
// What RegisterHotKey knows the shortcut by: the window only has the one
static constexpr int HOTKEY_ID = 1;
// What send_hotkey_press posts to the window
static constexpr UINT PRESS = WM_APP + 1;

// Windows' virtual-key code for key, a name shortcut_key_name gives
static UINT virtual_key(const std::string& key){
    // Letters and digits are their own capital ASCII
    if (key.size() == 1) return static_cast<unsigned char>(key[0]);
    if (key[0] == 'F' && key.size() <= 3) return VK_F1 + static_cast<UINT>(std::stoi(key.substr(1)) - 1);
    if (key == "Space") return VK_SPACE;
    if (key == "Insert") return VK_INSERT;
    if (key == "Delete") return VK_DELETE;
    if (key == "Home") return VK_HOME;
    if (key == "End") return VK_END;
    if (key == "PageUp") return VK_PRIOR;
    if (key == "PageDown") return VK_NEXT;
    if (key == "Up") return VK_UP;
    if (key == "Down") return VK_DOWN;
    if (key == "Left") return VK_LEFT;
    if (key == "Right") return VK_RIGHT;
    return VK_PAUSE;
}

// The hidden window's messages, which FLTK's event loop hands it, on the
// UI thread. Its user data is the GlobalHotkey's on_press.
static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam){
    if ((message == WM_HOTKEY && wparam == HOTKEY_ID) || message == PRESS){
        auto* on_press = reinterpret_cast<std::function<void()>*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        // An exception can't unwind through the Windows code that called this
        try {
            if (on_press) (*on_press)();
        } catch (const std::exception& e){
            std::cerr << std::format("Couldn't capture: {}\n", e.what());
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

GlobalHotkey::GlobalHotkey(std::function<void()> on_press) : on_press(std::move(on_press)){
    // A message-only window: never shown, and only found by asking for
    // those. Its messages still come through any message loop, including
    // the one Windows runs while a window is dragged.
    if (FindWindowExW(HWND_MESSAGE, nullptr, WINDOW_CLASS, nullptr))
        throw std::runtime_error("Another copy of the app is running, and it gets the capture shortcut");

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{};
    type.lpfnWndProc = window_proc;
    type.hInstance = instance;
    type.lpszClassName = WINDOW_CLASS;
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error(std::format("Couldn't listen for the capture shortcut: error {}", GetLastError()));

    const HWND hwnd = CreateWindowExW(0, WINDOW_CLASS, L"Prototype", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!hwnd) throw std::runtime_error(std::format("Couldn't listen for the capture shortcut: error {}", GetLastError()));
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&this->on_press));
    window = hwnd;
}

GlobalHotkey::~GlobalHotkey(){
    const HWND hwnd = static_cast<HWND>(window);
    if (registered) UnregisterHotKey(hwnd, HOTKEY_ID);
    DestroyWindow(hwnd);
}

void GlobalHotkey::set_shortcut(const std::string& text){
    const HWND hwnd = static_cast<HWND>(window);
    if (registered){
        UnregisterHotKey(hwnd, HOTKEY_ID);
        registered = false;
    }
    if (text.empty()) return;

    const std::optional<Shortcut> shortcut = parse_shortcut(text);
    if (!shortcut) throw std::runtime_error(std::format("{} can't be a shortcut", text));
    // Held down, it presses once, not again with each repeat
    UINT modifiers = MOD_NOREPEAT;
    if (shortcut->win) modifiers |= MOD_WIN;
    if (shortcut->ctrl) modifiers |= MOD_CONTROL;
    if (shortcut->alt) modifiers |= MOD_ALT;
    if (shortcut->shift) modifiers |= MOD_SHIFT;

    if (!RegisterHotKey(hwnd, HOTKEY_ID, modifiers, virtual_key(shortcut->key))){
        const DWORD error = GetLastError();
        if (error == ERROR_HOTKEY_ALREADY_REGISTERED)
            throw std::runtime_error(std::format("{} is taken, by Windows or another app. Choose another.", text));
        throw std::runtime_error(std::format("Couldn't make {} the capture shortcut: error {}", text, error));
    }
    registered = true;
}

void send_hotkey_press(){
    const HWND hwnd = FindWindowExW(HWND_MESSAGE, nullptr, WINDOW_CLASS, nullptr);
    if (!hwnd) throw std::runtime_error("The app isn't running");
    if (!PostMessageW(hwnd, PRESS, 0, 0))
        throw std::runtime_error(std::format("Couldn't send the capture to the app: error {}", GetLastError()));
}

std::string capture_command(){
    return "";
}

#else

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// FLTK
#include <FL/Fl.H>

// What a press is sent as. One byte, so it arrives whole or not at all.
static constexpr char PRESS = 'c';

// Closes the socket when it goes out of scope, unless released
struct Socket {
    int fd;
    explicit Socket(int fd) : fd(fd) {}
    ~Socket(){ if (fd >= 0) close(fd); }
    int release(){ int out = fd; fd = -1; return out; }
};

// $XDG_RUNTIME_DIR/prototype.sock. That folder belongs to the user alone
// (mode 0700) and is emptied at logout, so a socket left behind by a
// crash doesn't outlive the session.
static sockaddr_un socket_address(){
    const char* dir = std::getenv("XDG_RUNTIME_DIR");
    if (!dir || *dir != '/') throw std::runtime_error("XDG_RUNTIME_DIR isn't set");
    const std::string path = (std::filesystem::path(dir) / "prototype.sock").string();

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path))
        throw std::runtime_error(std::format("The socket's path is too long: {}", path));
    path.copy(address.sun_path, path.size());
    return address;
}

static int new_socket(){
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) throw std::runtime_error(std::format("Couldn't create a socket: {}", std::strerror(errno)));
    return fd;
}

// Whether a running copy of the app is listening at address
static bool connect_to(int fd, const sockaddr_un& address){
    return connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
}

GlobalHotkey::GlobalHotkey(std::function<void()> on_press) : on_press(std::move(on_press)){
    const sockaddr_un address = socket_address();
    Socket fd(new_socket());

    if (bind(fd.fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0){
        if (errno != EADDRINUSE)
            throw std::runtime_error(std::format("Couldn't listen for the capture shortcut: {}", std::strerror(errno)));
        // Either another copy is running, or one crashed and left its
        // socket behind. Only the second can be taken over.
        Socket probe(new_socket());
        if (connect_to(probe.fd, address))
            throw std::runtime_error("Another copy of the app is running, and it gets the capture shortcut");
        unlink(address.sun_path);
        if (bind(fd.fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
            throw std::runtime_error(std::format("Couldn't listen for the capture shortcut: {}", std::strerror(errno)));
    }
    if (listen(fd.fd, 4) != 0)
        throw std::runtime_error(std::format("Couldn't listen for the capture shortcut: {}", std::strerror(errno)));

    listener = fd.release();
    Fl::add_fd(listener, FL_READ, on_connection, this);
}

GlobalHotkey::~GlobalHotkey(){
    if (listener < 0) return;
    Fl::remove_fd(listener);
    close(listener);
    // So the next start doesn't have to find out it's stale
    unlink(socket_address().sun_path);
}

void GlobalHotkey::on_connection(int fd, void* data){
    const int client = accept4(fd, nullptr, nullptr, SOCK_CLOEXEC);
    if (client < 0) return;
    // The byte may not be there yet, so it's read once it is
    Fl::add_fd(client, FL_READ, on_message, data);
}

void GlobalHotkey::on_message(int fd, void* data){
    char message = 0;
    const ssize_t n = read(fd, &message, 1);
    Fl::remove_fd(fd);
    close(fd);
    // Anything else, e.g. a connection only checking whether the app runs,
    // is ignored
    if (n == 1 && message == PRESS) static_cast<GlobalHotkey*>(data)->on_press();
}

void GlobalHotkey::set_shortcut(const std::string&){}

void send_hotkey_press(){
    const sockaddr_un address = socket_address();
    Socket fd(new_socket());
    if (!connect_to(fd.fd, address)) throw std::runtime_error("The app isn't running");
    if (write(fd.fd, &PRESS, 1) != 1)
        throw std::runtime_error(std::format("Couldn't send the capture to the app: {}", std::strerror(errno)));
}

std::string capture_command(){
    std::error_code error;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", error);
    const std::string path = error ? "prototype" : exe.string();
    // Hyprland and most desktops run it through sh
    const std::string quoted = path.find(' ') == std::string::npos ? path : std::format("'{}'", path);
    return std::format("{} {}", quoted, CAPTURE_ARG);
}

#endif
