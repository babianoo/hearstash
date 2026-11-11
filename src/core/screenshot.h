#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/image.h"

// A screenshot of the whole monitor the user is working on, taken now:
// the one with the focused window, so a video is on it whatever else is.
// Takes about a tenth of a second with grim or GDI.
//
// On Linux it runs grim first, which works on wlroots desktops (Hyprland,
// Sway); on Hyprland it asks hyprctl which monitor has the focus, and
// elsewhere takes all of them. On KDE it then asks KWin itself for the
// monitor with the focus, which takes a few hundredths of a second, if
// KWin lets the app. When neither can (GNOME, X11, a KWin that says no)
// it asks the desktop through the XDG Screenshot portal, which takes all
// the monitors, and slower: about 2 s on KDE, most of it the portal
// saving a PNG file. On KDE that's then cut down to the monitor with the
// focus, which KWin names, where kscreen-doctor says it is; elsewhere it
// stays all of them. The portal may ask the user once whether to allow
// it. On Windows it copies the
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

// Which program has the focus, so what a screenshot shows can be told
// apart by game: on Windows the file name of the foreground window's
// program, e.g. "fate.exe"; on Hyprland the focused window's class (for a
// Wine or Proton game, its .exe's name or steam_app_<id>). Empty
// elsewhere, e.g. on KDE or GNOME, which don't say, or if it can't tell.
// Takes a few milliseconds.
std::string focused_app();

// Parses a binary PPM (P6) with 8-bit samples, as grim -t ppm writes it.
// Throws std::runtime_error if it isn't one.
Image parse_ppm(const std::vector<std::uint8_t>& data);

#ifndef _WIN32
// Where the monitor named output (e.g. "DP-1") is in a screenshot of all
// the monitors, image_w x image_h: the box around them all, laid out as
// layout_json says, which is what kscreen-doctor -j prints. Empty if
// there's no such monitor, or the screenshot's shape doesn't match the
// layout (then it can't be told where the monitor is in it).
Rect monitor_in_screenshot(std::string_view layout_json, std::string_view output, int image_w, int image_h);

// Decodes a PNG file's bytes, as the screenshot portal saves them, to 8-bit
// RGB. Throws std::runtime_error if it isn't one.
Image parse_png(const std::vector<std::uint8_t>& data);

// The pixels KWin hands over for a screenshot as an Image: height rows of
// stride bytes, 4 bytes a pixel, in the Qt pixel format numbered format
// (QImage::Format). Their alpha is left out: nothing is behind a screen.
// Throws std::runtime_error if it's a format this doesn't know, or there
// are too few pixels for the size.
Image qt_pixels_to_image(std::span<const std::uint8_t> pixels, int width, int height, int stride, unsigned format);
#endif
