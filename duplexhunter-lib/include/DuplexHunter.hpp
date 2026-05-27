#ifndef DUPLEX_HUNTER_HPP
#define DUPLEX_HUNTER_HPP

#include "export.h"
#include "DuplexHunterConfig.hpp"
#include "FileScanner.hpp"
#include "FileMatch.hpp"
#include "DuplexHunterProgressInfo.hpp"
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <functional>

class DUPLEXHUNTER_API DuplexHunter
{
public:
    DUPLEXHUNTER_API DuplexHunter(const DuplexHunterConfig& config);
    DUPLEXHUNTER_API ~DuplexHunter();

    DUPLEXHUNTER_API void run();
    DUPLEXHUNTER_API void scan();
    DUPLEXHUNTER_API void hashFiles();
    DUPLEXHUNTER_API void match();
    DUPLEXHUNTER_API void exportResults();

    DUPLEXHUNTER_API void setProgressCallback(std::function<void(DuplexHunterProgressInfo)> cb);

private:
    DuplexHunterConfig config;

    std::vector<std::shared_ptr<FileScanner>> scanners;
    std::unique_ptr<FileMatch> matcher;

    std::function<void(DuplexHunterProgressInfo)> progressCallback;

    std::size_t totalFiles = 0;
    std::size_t processedFiles = 0;
};

#endif // DUPLEX_HUNTER_HPP