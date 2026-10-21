// FLTK
#include <FL/Fl.H>

#include "ui/theme.h"
#include "ui/main_window.h"

int main(int argc, char** argv){
    init_theme();

    MainWindow main_win;
    main_win.show(argc, argv);

    return Fl::run();
}
