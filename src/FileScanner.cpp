#include <FileScanner.hpp>

#include <iostream>

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

void FileScanner::run() {
    scan(path, depth);
}

std::vector<std::unique_ptr<FileEntry>>::iterator FileScanner::getBeginIterator() {
    return files.begin();
}

std::vector<std::unique_ptr<FileEntry>>::iterator FileScanner::getEndIterator() {
    return files.end();
}

void FileScanner::scan(const std::string& it_path, uint16_t it_depth) {
    try {
        for (auto& entry : fs::directory_iterator(it_path)) {
            try {
                if (fs::is_directory(entry) && it_depth > MIN_DEPTH) {
                    scan(entry.path(), it_depth - 1);
                } else if (fs::is_regular_file(entry)) {
                    files.push_back(std::make_unique<FileEntry>(entry.path(), this->fastHash));
                }
            } catch (const fs::filesystem_error& e) {
                std::cerr << "Warning: No access to file entry: " << entry.path() << std::endl;
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Warning: No access to directory: " << it_path << std::endl;
    }
}

