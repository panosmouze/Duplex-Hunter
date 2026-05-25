#ifndef _FILE_SCANNER_HPP_
#define _FILE_SCANNER_HPP_

#include "FileEntry.hpp"

#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>

namespace fs = std::filesystem;

#define MAX_DEPTH UINT16_MAX
#define MIN_DEPTH 0

class FileScanner {
public:
    FileScanner(const std::string& path, bool fastHash = false, uint16_t depth = MAX_DEPTH);
    ~FileScanner();

    void runScan();
    void runHashes(std::function<void()> cb);
    std::size_t getNumberOfFiles();

    std::vector<std::unique_ptr<FileEntry>>::iterator getBeginIterator();
    std::vector<std::unique_ptr<FileEntry>>::iterator getEndIterator();
    std::string getPath();

private:
    void scan(const std::string& path, uint16_t depth);

private:
    std::vector<std::unique_ptr<FileEntry>> files;
    std::string path;
    uint16_t depth;
    bool fastHash;
};

#endif //_FILE_SCANNER_HPP_