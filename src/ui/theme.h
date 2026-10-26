#pragma once

// FLTK
#include <FL/Enumerations.H>

// Height of pill-shaped widgets like the search bar.
inline constexpr int PILL_H = 30;

// Grey for text that's less important than what's around it, like a
// reading or a note under a setting
inline const Fl_Color DIM = fl_rgb_color(70, 70, 70);
// Blue for what's selected or in progress
inline const Fl_Color ACCENT = fl_rgb_color(50, 110, 240);

// A window's sidebar: a bit darker than the window, so it stands apart
// from what's beside it. A function, not a constant: the background color
// can still change (e.g. with -bg) before the first window is created.
Fl_Color sidebar_color();

// Sets the FLTK scheme.
// Call once in main(), before any window is created.
void init_theme();
