#pragma once

// FLTK
#include <FL/Enumerations.H>

// Pill-shaped box with room for the text. Its text inset is sized for
// widgets PILL_H tall.
inline constexpr Fl_Boxtype PILL_BOX = FL_FREE_BOXTYPE;
inline constexpr int PILL_H = 30;

// Sets the FLTK scheme and registers the custom boxtypes above.
// Call once in main(), before any window is created.
void init_theme();
