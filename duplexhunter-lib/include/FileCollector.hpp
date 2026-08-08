#ifndef _FILE_COLLECTOR_HPP_
#define _FILE_COLLECTOR_HPP_

#include "FileEntry.hpp"

#include <vector>
#include <memory>

class FileCollector {
public:
    FileCollector();
    ~FileCollector();

    void addFile(std::unique_ptr<FileEntry> entry);
    std::size_t getNumberOfFiles();

    std::vector<std::unique_ptr<FileEntry>>::iterator getBeginIterator();
    std::vector<std::unique_ptr<FileEntry>>::iterator getEndIterator();

private:
    std::vector<std::unique_ptr<FileEntry>> files;
};

#endif //_FILE_COLLECTOR_HPP_
