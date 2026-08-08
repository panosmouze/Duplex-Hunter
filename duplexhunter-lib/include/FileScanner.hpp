#ifndef _FILE_SCANNER_HPP_
#define _FILE_SCANNER_HPP_

#include "FileEntry.hpp"
#include "FileCollector.hpp"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

#define MAX_DEPTH UINT16_MAX
#define MIN_DEPTH 0

class FileScanner {
public:
    FileScanner(FileCollector& collector, const std::string& path, uint16_t depth = MAX_DEPTH);
    ~FileScanner();

    void runScan();
    std::string getPath();

private:
    void scan(const std::string& path, uint16_t depth);

private:
    FileCollector& collector;
    std::string path;
    uint16_t depth;
};

#endif //_FILE_SCANNER_HPP_
