#pragma once

#include <string>
#include <string_view>

// What to look up for text selected in a transcript: the selection with
// the spaces and punctuation around it taken off, e.g. "「天気」、" gives
// "天気". Empty if there's nothing else in it, e.g. only "。" or a "♪"
// whisper wrote for music, which isn't worth a search.
std::string lookup_text(std::string_view selection);
