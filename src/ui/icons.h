#pragma once

// SVG icons, stored as text so there are no image files to ship or find at
// runtime. Load one with Fl_SVG_Image(nullptr, ICON_...), then scale() it.
// All are drawn on a 16x16 grid in the same dark grey.

// Microphone: capsule, holder arc, stand and base
inline constexpr const char* ICON_MIC =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none' "
    "stroke='#444' stroke-width='1.4' stroke-linecap='round'>"
    "<rect x='5.75' y='1.5' width='4.5' height='8' rx='2.25'/>"
    "<path d='M3.5 7.5a4.5 4.5 0 0 0 9 0'/>"
    "<path d='M8 12v2.5M5.5 14.5h5'/>"
    "</svg>";
