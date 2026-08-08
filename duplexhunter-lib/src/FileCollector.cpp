#include "FileCollector.hpp"

FileCollector::FileCollector() {

}

FileCollector::~FileCollector() {

}

void FileCollector::addFile(std::unique_ptr<FileEntry> entry) {
    files.push_back(std::move(entry));
}

std::size_t FileCollector::getNumberOfFiles() {
    return files.size();
}

std::vector<std::unique_ptr<FileEntry>>::iterator FileCollector::getBeginIterator() {
    return files.begin();
}

std::vector<std::unique_ptr<FileEntry>>::iterator FileCollector::getEndIterator() {
    return files.end();
}
