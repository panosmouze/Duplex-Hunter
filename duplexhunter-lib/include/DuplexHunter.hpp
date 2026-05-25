#ifndef DUPLEX_HUNTER_HPP
#define DUPLEX_HUNTER_HPP

#include "export.h"
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <functional>

class FileScanner;
class FileMatch;
class ProgressInfo;

class DUPLEX_API DuplexHunter
{
public:
    DUPLEX_API DuplexHunter(const std::vector<std::string>& paths, uint16_t depth, bool fastHash);
    DUPLEX_API ~DuplexHunter();

    DUPLEX_API void run();
    DUPLEX_API void scan();
    DUPLEX_API void hashFiles();
    DUPLEX_API void match();
    DUPLEX_API void exportResults(const std::string& path);

    DUPLEX_API void setProgressCallback(std::function<void(ProgressInfo)> cb);

private:
    std::vector<std::string> paths;
    uint16_t depth;
    bool fastHash;

    std::vector<std::shared_ptr<FileScanner>> scanners;
    std::unique_ptr<FileMatch> matcher;

    std::function<void(ProgressInfo)> progressCallback;

    std::size_t totalFiles = 0;
    std::size_t processedFiles = 0;
};

#endif // DUPLEX_HUNTER_HPP