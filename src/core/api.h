#pragma once

// The services a search can go to. Same APIs as japanese-helper. Order
// matches the API menu.
enum class Api { Jisho, DeepL, MyMemory };

// Indexed by Api, as they're shown to the user
inline constexpr const char* API_NAMES[] = {"Jisho", "DeepL", "MyMemory"};
// Indexed by Api, as they're saved in files. Never rename one: files
// saved before would no longer load.
inline constexpr const char* API_IDS[] = {"jisho", "deepl", "mymemory"};
