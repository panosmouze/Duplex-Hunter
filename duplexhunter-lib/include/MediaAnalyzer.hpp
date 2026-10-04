#ifndef _MEDIA_ANALYZER_HPP_
#define _MEDIA_ANALYZER_HPP_

#include "FileAnalyzer.hpp"

// Video, audio and image statistics using the FFmpeg libraries.
// - basic:   container and stream headers, image metadata.
// - packets: reads every packet without decoding, for integrity, bitrate,
//            keyframe and timing stats; seeks to decode the perceptual hash frames.
// - deep:    decodes everything, for decode errors, black frames and audio levels.
class MediaAnalyzer : public FileAnalyzer {
public:
    MediaAnalyzer();

    std::string name() const override;
    bool canHandle(const FileType& type) const override;
    nlohmann::json analyze(const fs::path& path, const FileType& type, AnalysisLevel level) override;

    // Number of subtitle lines and the time span they cover, read without decoding.
    static nlohmann::json subtitleStats(const fs::path& path);
};

#endif //_MEDIA_ANALYZER_HPP_
