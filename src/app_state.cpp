#include "app_state.h"

AppState::AppState(){
    // main_win is constructed (as a member) before this body runs, so it's
    // the current group here: widgets created below are added to it.
    main_win.end();
    main_win.resizable(main_win);
}

void AppState::show(int argc, char** argv){
    main_win.show(argc, argv);
}
