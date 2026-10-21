// FLTK
#include <FL/Fl.H>

#include "app_state.h"

int main(int argc, char** argv){
    AppState app;
    app.show(argc, argv);

    return Fl::run();
}
