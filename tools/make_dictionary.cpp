// Makes the dictionary file the app ships with from a dictionary's zip:
//
//   make_dictionary jitendex-yomitan.zip jitendex.hsdict
//
// Run by the build (see CMakeLists.txt), and by hand to try a zip out. The
// app does the same itself when it updates its dictionary.

#include <cstdio>
#include <exception>

#include "core/dictionary.h"
#include "core/dictionary_import.h"

int main(int argc, char** argv){
    if (argc != 3){
        std::fprintf(stderr, "Usage: %s <dictionary.zip> <output file>\n", argv[0]);
        return 2;
    }
    try {
        // About half a minute for Jitendex, with nothing to show for it
        // until it's done: a build's log is no place for a progress bar
        const DictionaryInfo info = import_dictionary(argv[1], argv[2]);
        const Dictionary dictionary(argv[2]);
        std::printf("%s, revision %s: %zu rows\n", info.title.c_str(), info.revision.c_str(), dictionary.row_count());
    } catch (const std::exception& e){
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    return 0;
}
