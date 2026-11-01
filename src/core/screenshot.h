#pragma once

#include <cstdint>
#include <vector>

#include "core/image.h"

// A screenshot of the whole monitor the user is working on, taken now:
// the one with the focused window, so a video is on it whatever else is.
// Takes about a tenth of a second.
//
// On Linux it runs grim, which works on wlroots desktops (Hyprland, Sway);
// on Hyprland it asks hyprctl which monitor has the focus, and elsewhere
// takes all of them. On Windows, the GDI capture isn't done yet.
//
// DRM-protected video (Netflix and the like) comes out black whatever
// takes the screenshot. That's the system's choice, and this doesn't try
// to get around it.
//
// Throws std::runtime_error if it can't, e.g. grim isn't installed.
Image take_screenshot();

// Parses a binary PPM (P6) with 8-bit samples, as grim -t ppm writes it.
// Throws std::runtime_error if it isn't one.
Image parse_ppm(const std::vector<std::uint8_t>& data);
