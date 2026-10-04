#ifndef _FILE_ANALYSIS_HPP_
#define _FILE_ANALYSIS_HPP_

#include "FileAnalyzer.hpp"
#include "FileTypeDetector.hpp"
#include "SubtitleFinder.hpp"
#include "FileMatch.hpp"

#include <nlohmann/json.hpp>
#include <memory>
#include <vector>

class FileAnalysis {
public:
    FileAnalysis(AnalysisLevel level);
    ~FileAnalysis();

    // Files of a group share the same content, so content is analyzed once
    // using the first file, while filesystem stats are collected per file.
    nlohmann::json analyze(const FileMatchGroup& group);

private:
    static nlohmann::json fileSystemStats(const fs::path& path);
    nlohmann::json externalSubtitles(const fs::path& video, double videoDuration);

private:
    AnalysisLevel level;
    FileTypeDetector detector;
    SubtitleFinder subtitleFinder;
    std::vector<std::unique_ptr<FileAnalyzer>> analyzers;
};

#endif //_FILE_ANALYSIS_HPP_
