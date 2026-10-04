#ifndef _SUBTITLE_FINDER_HPP_
#define _SUBTITLE_FINDER_HPP_

#include <nlohmann/json.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// Links videos to the external subtitle files stored next to them, by file name:
//   Movie.mkv  <->  Movie.srt, Movie.en.srt, Movie.el.forced.srt, Movie.en.sdh.srt
//   Movie.mkv  <->  Subs/2_English.srt          (when it is the only video in its folder)
//   Episode.mkv <-> Subs/Episode/2_English.srt
class SubtitleFinder {
public:
    static bool isSubtitle(const fs::path& path);
    static bool isVideo(const fs::path& path);

    // Subtitle files of a video, with format, language and flags parsed from their names.
    nlohmann::json subtitlesFor(const fs::path& video);

    // The video a subtitle file belongs to, if any.
    std::optional<fs::path> videoFor(const fs::path& subtitle);

private:
    struct Listing {
        std::vector<fs::path> files;
        std::vector<fs::path> dirs;
    };

    // Directory listings are cached, as every video in a folder needs the same one.
    const Listing& list(const fs::path& dir);
    std::vector<fs::path> videosIn(const fs::path& dir);
    nlohmann::json describe(const fs::path& subtitle, const std::string& nameTags);

private:
    std::unordered_map<std::string, Listing> listings;
};

#endif //_SUBTITLE_FINDER_HPP_
