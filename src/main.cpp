// FLTK
#include <FL/Fl.H>

#include "core/config_dir.h"
#include "core/http.h"
#include "ui/theme.h"
#include "ui/history_store.h"
#include "ui/main_window.h"
#include "ui/model_downloader.h"
#include "ui/settings_store.h"

int main(int argc, char** argv){
    // Before any thread exists: searches and downloads run on their own
    // threads
    http_global_init();
    // Turns on FLTK's thread support, which Fl::awake() needs
    Fl::lock();

    init_theme();

    // Declared before main_win, which uses it, so it's destroyed after it.
    // That's once Fl::run() returns, and it then saves a change still
    // waiting to be saved.
    SettingsStore settings(config_dir() / "settings.json");
    // The same goes for it. Owned here rather than by a window, so a
    // download keeps going while the settings window is closed. Quitting
    // cancels the downloads still running.
    ModelDownloader downloader;
    // The same goes for it
    HistoryStore history(state_dir() / "history.json");

    MainWindow main_win(settings, history, downloader);
    main_win.show(argc, argv);

    return Fl::run();
}
