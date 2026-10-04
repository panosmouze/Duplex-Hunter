#ifndef _GENERIC_ANALYZER_HPP_
#define _GENERIC_ANALYZER_HPP_

#include "FileAnalyzer.hpp"

// Content statistics applicable to any file type.
class GenericAnalyzer : public FileAnalyzer {
public:
    std::string name() const override;
    bool canHandle(const FileType& type) const override;
    nlohmann::json analyze(const fs::path& path, const FileType& type, AnalysisLevel level) override;
};

#endif //_GENERIC_ANALYZER_HPP_
