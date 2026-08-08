#include "FileScanner.hpp"

#include <iostream>
#include <functional>

FileScanner::FileScanner(const std::string& path, bool fastHash, uint16_t depth) {
    if (!fs::exists(path)) {
        throw std::invalid_argument("Error: Path does not exist: " + path);
    }
    this->path = path;
    this->depth = depth;
    this->fastHash = fastHash;
}

FileScanner::~FileScanner() {

}

void FileScanner::runScan() {
    scan(path, depth);
}

void FileScanner::runHashes(std::function<void()> cb) {
    std::size_t scannerTotal = this->files.size();
    std::size_t scannerProgress = 0;
    for (auto it_file = getBeginIterator(); it_file != getEndIterator(); ++it_file) {
        (*it_file)->calcHash(this->fastHash);
        cb();
    }
}

std::vector<std::unique_ptr<FileEntry>>::iterator FileScanner::getBeginIterator() {
    return files.begin();
}

std::vector<std::unique_ptr<FileEntry>>::iterator FileScanner::getEndIterator() {
    return files.end();
}

std::size_t FileScanner::getNumberOfFiles() {
    return files.size();
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
                    files.push_back(std::make_unique<FileEntry>(entry.path()));
                }
            } catch (const fs::filesystem_error& e) {
                std::cerr << "Warning: No access to file entry: " << entry.path() << std::endl;
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Warning: No access to directory: " << it_path << std::endl;
    }
}

