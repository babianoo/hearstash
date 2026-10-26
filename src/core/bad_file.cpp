#include "core/bad_file.h"

#include <format>
#include <iostream>
#include <system_error>

namespace fs = std::filesystem;

void set_aside_bad_file(const fs::path& file, std::string_view error, std::string_view instead){
    fs::path bad = file;
    bad += ".bad";
    // Not worth failing over: the app goes on without the file either way
    std::error_code ec;
    fs::rename(file, bad, ec);
    std::cerr << std::format("Couldn't read {}, so {}: {}\n", file.string(), instead, error);
    if (!ec) std::cerr << std::format("It was renamed to {}.\n", bad.string());
}
