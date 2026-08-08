#ifndef _FILE_HASH_FILTER_HPP_
#define _FILE_HASH_FILTER_HPP_

#include "FileFilter.hpp"

#include <functional>

class FileHashFilter : public FileFilter {
public:
    FileHashFilter(bool fastHash);

    std::vector<std::vector<FileEntry*>> apply(const std::vector<FileEntry*>& candidates) override;

    void setProgressCallback(std::function<void(FileEntry&)> cb);

private:
    bool fastHash;
    std::function<void(FileEntry&)> progressCallback;
};

#endif //_FILE_HASH_FILTER_HPP_
