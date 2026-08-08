#ifndef _FILE_FILTER_HPP_
#define _FILE_FILTER_HPP_

#include "FileEntry.hpp"

#include <vector>

class FileFilter {
public:
    virtual ~FileFilter() = default;

    virtual std::vector<std::vector<FileEntry*>> apply(const std::vector<FileEntry*>& candidates) = 0;
};

#endif //_FILE_FILTER_HPP_
