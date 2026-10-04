#ifndef _FILE_ANALYZER_HPP_
#define _FILE_ANALYZER_HPP_

#include "FileTypeDetector.hpp"
#include "DuplexHunterConfig.hpp"

#include <nlohmann/json.hpp>
#include <string>

class FileAnalyzer {
public:
    virtual ~FileAnalyzer() = default;

    // Key under which the analyzer results are stored in the "content" object.
    virtual std::string name() const = 0;
    virtual bool canHandle(const FileType& type) const = 0;
    virtual nlohmann::json analyze(const fs::path& path, const FileType& type, AnalysisLevel level) = 0;
};

#endif //_FILE_ANALYZER_HPP_
