#include "core/atomic_write.h"

#include <format>
#include <fstream>
#include <stdexcept>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

void write_file_atomically(const fs::path& file, std::string_view contents){
    fs::create_directories(file.parent_path());

    fs::path tmp = file;
    tmp += ".tmp";
#ifndef _WIN32
    // Only the user may read it: the settings hold the DeepL key, and the
    // ofstream alone would let everyone read it, as the umask usually
    // allows. Created empty with mode 0600 first, so it's never readable
    // by anyone else, not even for a moment; the ofstream then keeps that
    // mode. A leftover one is removed, since O_CREAT keeps an existing
    // file's mode. On Windows, AppData is already the user's alone.
    fs::remove(tmp);
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) throw std::runtime_error(std::format("Couldn't create {}", tmp.string()));
    ::close(fd);
#endif
    std::ofstream out(tmp);
    out << contents;
    out.close();
    if (!out) throw std::runtime_error(std::format("Couldn't write {}", tmp.string()));
    fs::rename(tmp, file);
}
