#ifndef _FILE_MATCH_HPP_
#define _FILE_MATCH_HPP_

#include "FileScanner.hpp"

#include <vector>
#include <utility>

class FileMatch {
public:
    FileMatch(const std::vector<std::shared_ptr<FileScanner>>& scanners);
    ~FileMatch();

    void run();

    std::vector<std::pair<uint64_t, std::vector<std::string>>> getDuplicates();
    std::vector<std::pair<uint64_t, std::vector<std::string>>> getUnique();
    std::vector<std::string> hashToFiles(uint64_t hash);

private:
    void match();
    void analyze();

private:
    std::vector<std::shared_ptr<FileScanner>> scanners;
    std::unordered_map<uint64_t, std::vector<std::string>> hashToPath;
    std::vector<std::pair<uint64_t, std::vector<std::string>>> duplicateHash;
    std::vector<std::pair<uint64_t, std::vector<std::string>>> uniqueHash;
};

#endif //_FILE_MATCH_HPP_