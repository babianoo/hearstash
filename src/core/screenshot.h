#pragma once

#include <cstdint>
#include <vector>

#include "core/image.h"

// A screenshot of the whole monitor the user is working on, taken now:
// the one with the focused window, so a video is on it whatever else is.
// Takes about a tenth of a second with grim or GDI.
//
// On Linux it runs grim first, which works on wlroots desktops (Hyprland,
// Sway); on Hyprland it asks hyprctl which monitor has the focus, and
// elsewhere takes all of them. When grim can't (KDE, GNOME, X11, or it
// isn't installed) it asks the desktop through the XDG Screenshot portal,
// which takes all the monitors, and slower: about 2 s on KDE. The portal
// may ask the user once whether to allow it. On Windows it copies the
// monitor with the foreground window through GDI, once a window of the
// app's has opened (which makes the app DPI-aware, so the monitor's size
// is in real pixels).
//
// DRM-protected video (Netflix and the like) comes out black whatever
// takes the screenshot. That's the system's choice, and this doesn't try
// to get around it.
//
// Throws std::runtime_error if it can't, e.g. neither grim nor the portal
// works.
Image take_screenshot();

// Parses a binary PPM (P6) with 8-bit samples, as grim -t ppm writes it.
// Throws std::runtime_error if it isn't one.
Image parse_ppm(const std::vector<std::uint8_t>& data);

#ifndef _WIN32
// Decodes a PNG file's bytes, as the screenshot portal saves them, to 8-bit
// RGB. Throws std::runtime_error if it isn't one.
Image parse_png(const std::vector<std::uint8_t>& data);
#endif
