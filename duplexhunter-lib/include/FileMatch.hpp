#ifndef _FILE_MATCH_HPP_
#define _FILE_MATCH_HPP_

#include "FileCollector.hpp"
#include "FileFilter.hpp"

#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <optional>
#include <unordered_map>
#include <functional>
#include <memory>

struct FileMatchGroup {
    uintmax_t size;
    std::optional<uint64_t> hash;
    std::vector<std::string> files;
};

void to_json(nlohmann::json& j, const FileMatchGroup& group);

class FileMatch {
public:
    FileMatch(FileCollector& collector, bool fastHash);
    ~FileMatch();

    void run();

    std::vector<FileMatchGroup> getDuplicates();
    std::vector<FileMatchGroup> getUnique();
    std::vector<std::string> hashToFiles(uint64_t hash);

    void setProgressCallback(std::function<void(const std::string& path, double progress)> cb);

private:
    void match();

private:
    FileCollector& collector;
    std::unique_ptr<FileFilter> sizeFilter;
    std::unique_ptr<FileFilter> hashFilter;

    std::unordered_map<uint64_t, std::vector<std::string>> hashToPath;
    std::vector<FileMatchGroup> duplicateGroups;
    std::vector<FileMatchGroup> uniqueGroups;

    std::function<void(const std::string& path, double progress)> progressCallback;
};

#endif //_FILE_MATCH_HPP_
