#ifndef _FILE_ENTRY_HPP_
#define _FILE_ENTRY_HPP_

#include <filesystem>
namespace fs = std::filesystem;

class FileEntry {
private:
    uint64_t hash;
    fs::path path;
    bool fastHash;

public:
    FileEntry(const fs::path& path, bool fastHash = false);
    ~FileEntry();

    uint64_t getHash();
    fs::path getPath();

    static uint64_t hashFile(const fs::path& filepath, bool fastHash);
};

#endif //_FILE_ENTRY_HPP_