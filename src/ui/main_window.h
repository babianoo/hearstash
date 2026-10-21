#pragma once

// FLTK
#include <FL/Fl_Double_Window.H>

// Only pointers to these are stored here, so the full headers are
// included in main_window.cpp instead.
class Fl_Input;
class Fl_Button;
class Fl_Widget;

// Same APIs as japanese-helper. Order matches the API menu.
enum class Api { Jisho, DeepL, MyMemory };

class MainWindow {
public:
    MainWindow();

    // One app, one window: copying makes no sense
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void show(int argc, char** argv);

private:
    // Owned by MainWindow as a member, so no new/delete anywhere.
    // When MainWindow dies, win dies and deletes all of its children.
    Fl_Double_Window win{900, 600, "Prototype"};

    // Created with new in the constructor and added to win, which
    // deletes them when it dies. MainWindow only keeps a handle to them.
    Fl_Input* search_input = nullptr;
    Fl_Button* search_btn = nullptr;
    Fl_Button* api_btn = nullptr;

    Api api = Api::Jisho;

    // Static because FLTK callbacks are plain functions; the MainWindow
    // comes in through data.
    static void on_api_btn(Fl_Widget* w, void* data);
};
