#include <exception>
#include <iostream>
#include <string_view>

// FLTK
#include <FL/Fl.H>
#include <FL/Fl_Window.H>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "core/config_dir.h"
#include "core/http.h"
#include "ui/capturer.h"
#include "ui/global_hotkey.h"
#include "ui/theme.h"
#include "ui/history_store.h"
#include "ui/main_window.h"
#include "ui/model_downloader.h"
#include "ui/settings_store.h"
#include "ui/shared_transcriber.h"
#include "ui/smooth_scroll.h"
#include "ui/stash_store.h"

int main(int argc, char** argv){
    // Run by the key the user bound to it: passes the press on to the app
    // that's running, and quits
    if (argc == 2)
        for (HotkeyAction action : HOTKEY_ACTIONS){
            if (std::string_view(argv[1]) != hotkey_arg(action)) continue;
            try {
                send_hotkey_press(action);
                return 0;
            } catch (const std::exception& e){
                std::cerr << "Couldn't capture: " << e.what() << '\n';
                return 1;
            }
        }

    // Before any thread exists: searches and downloads run on their own
    // threads
    http_global_init();
    // Turns on FLTK's thread support, which Fl::awake() needs
    Fl::lock();

    init_theme();

#ifdef _WIN32
    // The icon built into the .exe (resources/hearstash.rc), for every
    // window's title bar and taskbar button. Loaded at the sizes Windows
    // draws them at, so neither is scaled from the other.
    HINSTANCE exe = GetModuleHandleW(nullptr);
    auto load_icon = [exe](int width_metric, int height_metric){
        return static_cast<HICON>(LoadImageW(exe, L"APP_ICON", IMAGE_ICON,
            GetSystemMetrics(width_metric), GetSystemMetrics(height_metric), 0));
    };
    Fl_Window::default_icons(load_icon(SM_CXICON, SM_CYICON), load_icon(SM_CXSMICON, SM_CYSMICON));
#endif

    // Declared before main_win, which uses it, so it's destroyed after it.
    // That's once Fl::run() returns, and it then saves a change still
    // waiting to be saved.
    SettingsStore settings(config_dir() / "settings.json");
    // Every scrolling area's wheel speed, now and after each change. The
    // listener only calls into settings, so it never has to be removed.
    SmoothScroll::set_wheel_step(settings.get().general.scroll_speed);
    settings.add_listener([&settings]{ SmoothScroll::set_wheel_step(settings.get().general.scroll_speed); });
    // The same goes for it. Owned here rather than by a window, so a
    // download keeps going while the settings window is closed. Quitting
    // cancels the downloads still running.
    ModelDownloader downloader;
    // The same goes for it
    HistoryStore history(state_dir() / "history.json", settings.get().history.capacity);
    // The same goes for it. The whisper model the main window and the
    // capture window both transcribe with.
    SharedTranscriber transcriber;
    // The same goes for it. The lines stashed to look at later.
    StashStore stash(state_dir() / "stash");
    // The same goes for it. Keeps the last seconds of what the computer
    // plays from now until the app quits.
    Capturer capturer(settings, transcriber, stash);

    MainWindow main_win(settings, history, downloader, capturer, transcriber, stash);
    main_win.show(argc, argv);

    return Fl::run();
}
