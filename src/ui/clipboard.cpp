#include "ui/clipboard.h"

// FLTK
#include <FL/Fl.H>
#include <FL/Fl_Widget.H>
#include <FL/fl_config.h>

// HAVE_WAYLAND_CLIENT comes from CMakeLists.txt when wayland-client is installed
#if FLTK_USE_WAYLAND && defined(HAVE_WAYLAND_CLIENT)
#define OWN_CLIPBOARD 1
#endif

#if OWN_CLIPBOARD

#include <cerrno>
#include <csignal>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <unistd.h>
#include <wayland-client.h>

// FLTK
#include <FL/platform.H>

// What every function below shares. Null until clipboard_init() binds them,
// and for good under X11.
static wl_seat* seat = nullptr;
static wl_data_device_manager* manager = nullptr;
static wl_data_device* device = nullptr;

// Some compositors (not Hyprland) only take a copy whose serial came with
// an input event, so this is the latest key press's (or focus's).
static uint32_t last_serial = 0;

// What's on the clipboard while this module owns it, else null
static wl_data_source* source = nullptr;
static std::string text;

// --- Sending the text to a pasting app ---

// One paste in progress: text still to write into the app's pipe
struct Transfer {
    int fd;
    std::string data;
    size_t done = 0;
};

// Writes in pieces whenever the pipe has room, so a slow reader can't
// freeze the window
static void on_writable(int fd, void* data){
    Transfer* t = static_cast<Transfer*>(data);
    const ssize_t n = write(fd, t->data.data() + t->done, t->data.size() - t->done);
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;    // Try again later
    if (n > 0){
        t->done += n;
        if (t->done < t->data.size()) return;
    }

    // All written, or the reader went away
    Fl::remove_fd(fd, FL_WRITE);
    close(fd);
    delete t;
}

// --- Data source: the clipboard contents this module offers ---

// The fix: unlike FLTK, ignored. It's drag-and-drop feedback, which
// Hyprland also sends when an app pastes.
static void source_target(void*, wl_data_source*, const char*){}

// An app pastes: write the text into the pipe it sent
static void source_send(void*, wl_data_source*, const char*, int32_t fd){
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    // A copy of the text, in case it's replaced before the write ends
    Fl::add_fd(fd, FL_WRITE, on_writable, new Transfer{fd, text});
}

// Another app copied something
static void source_cancelled(void*, wl_data_source* s){
    wl_data_source_destroy(s);
    if (s == source) source = nullptr;
}

// Drag-and-drop only, which this module doesn't do
static void source_dnd_drop_performed(void*, wl_data_source*){}
static void source_dnd_finished(void*, wl_data_source*){}
static void source_action(void*, wl_data_source*, uint32_t){}

static const wl_data_source_listener source_listener = {
    .target = source_target,
    .send = source_send,
    .cancelled = source_cancelled,
    .dnd_drop_performed = source_dnd_drop_performed,
    .dnd_finished = source_dnd_finished,
    .action = source_action,
};

// --- Data device: only here to copy, so it frees what it's sent ---

static void device_data_offer(void*, wl_data_device*, wl_data_offer*){}
static void device_enter(void*, wl_data_device*, uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t, wl_data_offer* offer){
    if (offer) wl_data_offer_destroy(offer);
}
static void device_leave(void*, wl_data_device*){}
static void device_motion(void*, wl_data_device*, uint32_t, wl_fixed_t, wl_fixed_t){}
static void device_drop(void*, wl_data_device*){}
static void device_selection(void*, wl_data_device*, wl_data_offer* offer){
    if (offer) wl_data_offer_destroy(offer);
}

static const wl_data_device_listener device_listener = {
    .data_offer = device_data_offer,
    .enter = device_enter,
    .leave = device_leave,
    .motion = device_motion,
    .drop = device_drop,
    .selection = device_selection,
};

// --- Keyboard: only for last_serial. FLTK gets the same events on its own. ---

static void keyboard_keymap(void*, wl_keyboard*, uint32_t, int32_t fd, uint32_t){
    close(fd);
}
static void keyboard_enter(void*, wl_keyboard*, uint32_t serial, wl_surface*, wl_array*){
    last_serial = serial;
}
static void keyboard_leave(void*, wl_keyboard*, uint32_t, wl_surface*){}
static void keyboard_key(void*, wl_keyboard*, uint32_t serial, uint32_t, uint32_t, uint32_t){
    last_serial = serial;
}
static void keyboard_modifiers(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t){}

// Version 1, so there's no repeat_info
static const wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = nullptr,
};

// --- Setup ---

static void registry_global(void*, wl_registry* registry, uint32_t name, const char* interface, uint32_t){
    // Version 1 of both is all this needs. The first seat if there are several.
    if (std::strcmp(interface, wl_seat_interface.name) == 0 && !seat)
        seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
    else if (std::strcmp(interface, wl_data_device_manager_interface.name) == 0)
        manager = static_cast<wl_data_device_manager*>(
            wl_registry_bind(registry, name, &wl_data_device_manager_interface, 1));
}
static void registry_global_remove(void*, wl_registry*, uint32_t){}

static const wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

void clipboard_init(){
    wl_display* display = fl_wl_display();
    if (!display || device) return;    // X11, or already done

    // A pasting app that closes its pipe early would otherwise end this
    // whole program on the next write
    std::signal(SIGPIPE, SIG_IGN);

    // The registry gets its own queue so the wait below only handles its
    // events, not FLTK's
    wl_event_queue* queue = wl_display_create_queue(display);
    wl_registry* registry = wl_display_get_registry(display);
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(registry), queue);
    wl_registry_add_listener(registry, &registry_listener, nullptr);
    wl_display_roundtrip_queue(display, queue);    // Waits for the globals
    wl_registry_destroy(registry);

    // Bound from the registry, so they start in its queue. Back to the
    // default one, which FLTK's event loop handles.
    if (seat) wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(seat), nullptr);
    if (manager) wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(manager), nullptr);
    wl_event_queue_destroy(queue);
    if (!seat || !manager) return;

    wl_keyboard* keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(keyboard, &keyboard_listener, nullptr);
    device = wl_data_device_manager_get_data_device(manager, seat);
    wl_data_device_add_listener(device, &device_listener, nullptr);
}

void clipboard_copy(std::string_view new_text){
    if (!device){
        Fl::copy(new_text.data(), static_cast<int>(new_text.size()), 1);
        return;
    }

    if (source) wl_data_source_destroy(source);
    text = new_text;
    source = wl_data_device_manager_create_data_source(manager);
    wl_data_source_add_listener(source, &source_listener, nullptr);
    wl_data_source_offer(source, "text/plain;charset=utf-8");
    wl_data_source_offer(source, "text/plain");
    wl_data_device_set_selection(device, source, last_serial);
    wl_display_flush(fl_wl_display());
}

void clipboard_paste(Fl_Widget& receiver){
    if (!source){
        Fl::paste(receiver, 1);
        return;
    }

    // Our own text, handed over directly like FLTK does with its own
    Fl::e_text = text.data();
    Fl::e_length = static_cast<int>(text.size());
    Fl::e_clipboard_type = Fl::clipboard_plain_text;
    receiver.handle(FL_PASTE);
}

#else    // Not Wayland: FLTK's clipboard works

void clipboard_init(){}

void clipboard_copy(std::string_view text){
    Fl::copy(text.data(), static_cast<int>(text.size()), 1);
}

void clipboard_paste(Fl_Widget& receiver){
    Fl::paste(receiver, 1);
}

#endif
