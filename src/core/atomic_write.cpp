#include "core/atomic_write.h"

#include <format>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

void write_file_atomically(const fs::path& file, std::string_view contents){
    fs::create_directories(file.parent_path());

    fs::path tmp = file;
    tmp += ".tmp";
    std::ofstream out(tmp);
    out << contents;
    out.close();
    if (!out) throw std::runtime_error(std::format("Couldn't write {}", tmp.string()));
    fs::rename(tmp, file);
}
