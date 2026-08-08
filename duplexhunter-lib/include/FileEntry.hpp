#ifndef _FILE_ENTRY_HPP_
#define _FILE_ENTRY_HPP_

#include <filesystem>
namespace fs = std::filesystem;

class FileEntry {
private:
    uint64_t hash;
    fs::path path;
    uintmax_t size;

public:
    FileEntry(const fs::path& path, uintmax_t size);
    ~FileEntry();

    void calcHash(bool fastHash = false);
    uint64_t getHash();
    fs::path getPath();
    uintmax_t getSize();

    static uint64_t hashFile(const fs::path& filepath, bool fastHash);
};

#endif //_FILE_ENTRY_HPP_