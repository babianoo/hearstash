#pragma once

#include <string>

// What the settings window changes and the rest of the app reads. Only
// kept while the app runs: nothing is saved to a file yet.
struct Settings {
    // Empty when there's none
    std::string deepl_key;
};
