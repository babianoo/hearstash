#include "ui/fit_line.h"

#include <algorithm>

// FLTK
#include <FL/fl_draw.H>
#include <FL/fl_utf8.h>

std::string fit_line(std::string_view text, int max_w){
    static constexpr std::string_view ELLIPSIS = "…";
    const double ellipsis_w = fl_width(ELLIPSIS.data(), static_cast<int>(ELLIPSIS.size()));
    const size_t line_end = std::min(text.find('\n'), text.size());

    double width = 0;
    // End of the part that still leaves room for "…" after it
    size_t fits_with_ellipsis = 0;
    size_t i = 0;
    while (i < line_end){
        // At least 1, so a broken byte still moves the loop forward
        const size_t len = std::min<size_t>(std::max(1, fl_utf8len1(text[i])), line_end - i);
        width += fl_width(text.data() + i, static_cast<int>(len));
        if (width > max_w) break;
        i += len;
        if (width + ellipsis_w <= max_w) fits_with_ellipsis = i;
    }
    // All of it fit, and it's only one line
    if (i == text.size()) return std::string(text);
    return std::string(text.substr(0, fits_with_ellipsis)) + std::string(ELLIPSIS);
}
