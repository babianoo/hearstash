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

// Gear: 8-tooth cog outline with a round hole
inline constexpr const char* ICON_SETTINGS =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none' "
    "stroke='#444' stroke-width='1.2' stroke-linejoin='round'>"
    "<path d='M12.97 6.46 L14.90 6.82 L14.90 9.18 L12.97 9.54 L12.60 10.43 "
    "L13.72 12.04 L12.04 13.72 L10.43 12.60 L9.54 12.97 L9.18 14.90 L6.82 14.90 "
    "L6.46 12.97 L5.57 12.60 L3.96 13.72 L2.28 12.04 L3.40 10.43 L3.03 9.54 "
    "L1.10 9.18 L1.10 6.82 L3.03 6.46 L3.40 5.57 L2.28 3.96 L3.96 2.28 L5.57 3.40 "
    "L6.46 3.03 L6.82 1.10 L9.18 1.10 L9.54 3.03 L10.43 3.40 L12.04 2.28 "
    "L13.72 3.96 L12.60 5.57Z'/>"
    "<circle cx='8' cy='8' r='2.2'/>"
    "</svg>";

// Clock with an arrow going back around it: the search history
inline constexpr const char* ICON_HISTORY =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none' "
    "stroke='#444' stroke-width='1.2' stroke-linecap='round' stroke-linejoin='round'>"
    "<path d='M3.3 8A5.5 5.5 0 1 1 4.91 11.89'/>"
    "<path d='M1.7 6.6 3.3 8.2 4.9 6.6'/>"
    "<path d='M8.8 5v3.2l2.2 1.4'/>"
    "</svg>";

// Magnifying glass: in front of a box whose text searches or filters
inline constexpr const char* ICON_SEARCH =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none' "
    "stroke='#444' stroke-width='1.4' stroke-linecap='round'>"
    "<circle cx='6.8' cy='6.8' r='4.3'/>"
    "<path d='M10.1 10.1 14 14'/>"
    "</svg>";

// Eye: almond outline with a round pupil. Shown while the text is hidden;
// clicking it shows the text.
inline constexpr const char* ICON_SHOW =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none' "
    "stroke='#444' stroke-width='1.2' stroke-linejoin='round'>"
    "<path d='M1.5 8C3 5 5.3 3.5 8 3.5S13 5 14.5 8C13 11 10.7 12.5 8 12.5S3 11 1.5 8Z'/>"
    "<circle cx='8' cy='8' r='2.2'/>"
    "</svg>";

// The same eye, struck through. Shown while the text is visible; clicking
// it hides the text again.
inline constexpr const char* ICON_HIDE =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none' "
    "stroke='#444' stroke-width='1.2' stroke-linejoin='round' stroke-linecap='round'>"
    "<path d='M1.5 8C3 5 5.3 3.5 8 3.5S13 5 14.5 8C13 11 10.7 12.5 8 12.5S3 11 1.5 8Z'/>"
    "<circle cx='8' cy='8' r='2.2'/>"
    "<path d='M2.5 2.5l11 11'/>"
    "</svg>";
