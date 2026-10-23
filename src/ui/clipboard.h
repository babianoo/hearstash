#pragma once

#include <string_view>

class Fl_Widget;

// Ctrl+C / Ctrl+V clipboard, for use instead of Fl::copy(..., 1) and
// Fl::paste(..., 1).
//
// On Wayland, FLTK 1.4 drops its clipboard as soon as another app pastes
// on Hyprland: when an app asks for the text without first saying which
// type it accepts (GTK apps never do), Hyprland sends the copying app a
// wl_data_source.target event, which is meant for drag-and-drop. FLTK
// takes any target event without a mouse button held for a cancelled
// drag-and-drop and throws the clipboard away. So on Wayland this module
// offers the clipboard itself and ignores that event. On X11 it just
// calls FLTK.

// Needs the display to be open (a window shown). Call once, before any
// copy, so a key press can already be used to prove the copy was asked for.
void clipboard_init();

void clipboard_copy(std::string_view text);

// Pastes into receiver like Fl::paste(receiver, 1). Has to be used instead
// of it: FLTK pastes by blocking until the clipboard's owner sends the
// text, so pasting text this module offers would wait for itself forever.
void clipboard_paste(Fl_Widget& receiver);
