#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Reads the files in a zip archive, which is how dictionaries are
// distributed. Only what those need: files stored as they are or deflated,
// in an archive under 4 GiB. Encrypted and Zip64 archives are refused.
//
// The archive is opened for each read and closed after, so it can be
// deleted or replaced in between. Everything throws std::runtime_error
// with a message fit to show the user.
class ZipArchive {
public:
    struct Entry {
        // As the archive has it, with '/' between folders, e.g.
        // "term_bank_1.json". A folder's own entry ends in '/'.
        std::string name;
        // Of the file once read
        std::uint64_t size = 0;

    private:
        friend class ZipArchive;
        std::uint64_t stored_size = 0;
        // Where its header is in the archive
        std::uint64_t offset = 0;
        std::uint32_t crc = 0;
        std::uint16_t method = 0;
        bool encrypted = false;
    };

    // Reads the list of files, not the files
    explicit ZipArchive(std::filesystem::path file);

    // In the archive's order
    const std::vector<Entry>& entries() const { return entries_; }
    // The file with that name, or nullptr if there's none
    const Entry* find(std::string_view name) const;

    // The file's contents, checked against the checksum the archive has
    // for it, so damage from a bad download is noticed
    std::string read(const Entry& entry) const;

private:
    std::filesystem::path file;
    std::vector<Entry> entries_;
};
