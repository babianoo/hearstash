// FLTK
#include <FL/Fl.H>

#include "core/http.h"
#include "ui/theme.h"
#include "ui/main_window.h"

int main(int argc, char** argv){
    // Before any thread exists: searches run on their own threads
    http_global_init();
    // Turns on FLTK's thread support, which Fl::awake() needs
    Fl::lock();

    init_theme();

    MainWindow main_win;
    main_win.show(argc, argv);

    return Fl::run();
}
