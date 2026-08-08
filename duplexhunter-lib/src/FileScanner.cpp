#include "FileScanner.hpp"

#include <iostream>

FileScanner::FileScanner(FileCollector& collector, const std::string& path, uint16_t depth)
    : collector(collector)
{
    if (!fs::exists(path)) {
        throw std::invalid_argument("Error: Path does not exist: " + path);
    }
    this->path = path;
    this->depth = depth;
}

FileScanner::~FileScanner() {

}

void FileScanner::runScan() {
    scan(path, depth);
}

std::string FileScanner::getPath() {
    return this->path;
}

void FileScanner::scan(const std::string& it_path, uint16_t it_depth) {
    try {
        for (auto& entry : fs::directory_iterator(it_path)) {
            try {
                if (entry.is_directory() && it_depth > MIN_DEPTH) {
                    scan(entry.path(), it_depth - 1);
                } else if (entry.is_regular_file()) {
                    collector.addFile(std::make_unique<FileEntry>(entry.path(), entry.file_size()));
                }
            } catch (const fs::filesystem_error& e) {
                std::cerr << "Warning: No access to file entry: " << entry.path() << std::endl;
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Warning: No access to directory: " << it_path << std::endl;
    }
}
