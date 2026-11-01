#include "ui/global_hotkey.h"

#include <stdexcept>
#include <utility>

#ifdef _WIN32

// TODO(Windows): RegisterHotKey on the main window's HWND (fl_xid), and
// WM_HOTKEY caught with Fl::add_system_handler. The key combination is a
// setting, and failing to register it (another app has it) is reported.
GlobalHotkey::GlobalHotkey(std::function<void()> on_press) : on_press(std::move(on_press)){
    throw std::runtime_error("The capture shortcut isn't done on Windows yet");
}

GlobalHotkey::~GlobalHotkey() = default;
void GlobalHotkey::on_connection(int, void*){}
void GlobalHotkey::on_message(int, void*){}

void send_hotkey_press(){
    throw std::runtime_error("The capture shortcut isn't done on Windows yet");
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
