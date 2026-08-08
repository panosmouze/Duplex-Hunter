#ifndef _FILE_SIZE_FILTER_HPP_
#define _FILE_SIZE_FILTER_HPP_

#include "FileFilter.hpp"

class FileSizeFilter : public FileFilter {
public:
    std::vector<std::vector<FileEntry*>> apply(const std::vector<FileEntry*>& candidates) override;
};

#endif //_FILE_SIZE_FILTER_HPP_
