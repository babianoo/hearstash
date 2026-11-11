#pragma once

#include <vector>

#include "core/word_entry.h"
#include "ui/wrapped_text.h"

// A lookup's results as styled text for a WrappedText, a bit like
// jisho.org: each word big and bold with its reading in grey, then its
// numbered meanings, then its other forms; and last, small, which
// dictionary they're from. Shown by the main window and the capture
// window.
std::vector<TextSpan> format_entries(const std::vector<WordEntry>& entries);
