#include "ui/bring_to_front.h"

// FLTK
#include <FL/Fl.H>
#include <FL/Fl_Window.H>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
// Already defined by MinGW's C++ library, so std::min isn't broken
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// fl_xid(), the window's HWND
#include <FL/platform.H>

// Seconds between checks for whether the user has switched to another app
static constexpr double TOPMOST_TICK = 0.25;

static bool is_topmost(HWND window){
    return (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

// Whether window belongs to this app
static bool is_ours(HWND window){
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    return process == GetCurrentProcessId();
}

// While win is topmost: once another app has the foreground, it stops
// being, so it doesn't cover that app
static void on_topmost_tick(void* data){
    Fl_Window* win = static_cast<Fl_Window*>(data);
    // Hidden, the HWND is gone, and with it the topmost
    if (!win->shown()) return;
    const HWND foreground = GetForegroundWindow();
    if (foreground && !is_ours(foreground)){
        SetWindowPos(fl_xid(win), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }
    Fl::repeat_timeout(TOPMOST_TICK, on_topmost_tick, data);
}

void bring_to_front(Fl_Window& win){
    const HWND window = fl_xid(&win);
    if (!window) return;
    const HWND foreground = GetForegroundWindow();

    // Only above a topmost window: otherwise a card window, which isn't,
    // would open behind this one
    if (foreground && foreground != window && !is_ours(foreground) && is_topmost(foreground)){
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        Fl::remove_timeout(on_topmost_tick, &win);
        Fl::add_timeout(TOPMOST_TICK, on_topmost_tick, &win);
    }

    // Usually allowed: the app just got the shortcut's press, which lets
    // it take the foreground
    if (SetForegroundWindow(window) || GetForegroundWindow() == window) return;

    // Refused. Sharing the input state of the thread that has the
    // foreground lets it through.
    const DWORD self = GetCurrentThreadId();
    const DWORD other = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    if (!other || other == self || !AttachThreadInput(self, other, TRUE)) return;
    BringWindowToTop(window);
    SetForegroundWindow(window);
    SetFocus(window);
    AttachThreadInput(self, other, FALSE);
}

void stop_bringing_to_front(Fl_Window& win){
    Fl::remove_timeout(on_topmost_tick, &win);
}

#else

void bring_to_front(Fl_Window&){}

void stop_bringing_to_front(Fl_Window&){}

#endif
