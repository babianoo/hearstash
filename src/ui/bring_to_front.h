#pragma once

class Fl_Window;

// Brings win, just shown, in front of the app the user is in and gives it
// the focus, for a window a global shortcut opens: e.g. from a fullscreen
// game.
//
// Only does anything on Windows, where showing a window isn't enough:
// - Windows' focus-stealing prevention can refuse it the foreground, and
//   then shows it behind the app that has it.
// - A borderless fullscreen game is often topmost, above every window that
//   isn't. Then win is made topmost too, until the user switches to
//   another app.
// - A game with the screen to itself (exclusive fullscreen) minimizes
//   when win takes the foreground, as it does on Alt+Tab.
//
// On Linux the compositor decides, and a window shown anew already comes
// up in front.
void bring_to_front(Fl_Window& win);

// Stops what bring_to_front left running for win, for its destructor
void stop_bringing_to_front(Fl_Window& win);
