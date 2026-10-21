#pragma once

// Height of pill-shaped widgets like the search bar.
inline constexpr int PILL_H = 30;

// Sets the FLTK scheme.
// Call once in main(), before any window is created.
void init_theme();
