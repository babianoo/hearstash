#pragma once

#include <string>
#include <string_view>

// text's first line, cut to fit max_w pixels in the current font, ending
// in "…" if anything was left out. Measures one character at a time, so a
// 5000-character text costs no more than the part that fits.
std::string fit_line(std::string_view text, int max_w);
