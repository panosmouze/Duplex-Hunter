#ifndef _FILE_TYPE_DETECTOR_HPP_
#define _FILE_TYPE_DETECTOR_HPP_

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct magic_set;

struct FileType {
    std::string mime;
    std::string encoding;
    std::string description;
    // Extensions libmagic associates with the detected content (empty if unknown).
    std::vector<std::string> extensions;
};

class FileTypeDetector {
public:
    FileTypeDetector();
    ~FileTypeDetector();

    FileTypeDetector(const FileTypeDetector&) = delete;
    FileTypeDetector& operator=(const FileTypeDetector&) = delete;

    FileType detect(const fs::path& path);

    // Returns false if the path extension is not one of the known extensions for its content.
    static bool extensionMatches(const fs::path& path, const FileType& type);

private:
    std::string query(const fs::path& path, int flags);

private:
    magic_set* cookie;
};

#endif //_FILE_TYPE_DETECTOR_HPP_
