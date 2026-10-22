#pragma once

#include <string>
#include <string_view>

// Translates text into English with DeepL. The source language is detected
// automatically. Free keys (ending in ":fx") and Pro keys both work.
// Blocks until the request finishes, so don't call it on the UI thread.
// Throws std::runtime_error with a readable message on any failure.
std::string deepl_translate(std::string_view text, const std::string& api_key);
