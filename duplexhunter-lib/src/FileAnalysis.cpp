#include "FileAnalysis.hpp"
#include "GenericAnalyzer.hpp"
#ifdef DUPLEXHUNTER_HAS_FFMPEG
#include "MediaAnalyzer.hpp"
#endif

#include <sys/stat.h>
#include <sys/xattr.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <ctime>
#include <cstdio>
#include <set>

// Subtitles may end a little after the video, but not by more than this.
static constexpr double SUBTITLE_OVERRUN = 30.0;
// Subtitles ending before this share of the video are probably for a different cut.
static constexpr double SUBTITLE_MIN_COVERAGE = 0.6;

static const std::set<std::string> TEXT_ENCODINGS_OK = { "utf-8", "us-ascii", "utf-16le", "utf-16be" };

static nlohmann::json formatTime(int64_t seconds) {
    std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
    if (!gmtime_r(&t, &tm)) return nullptr;
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

static std::string formatDuration(double seconds) {
    int total = static_cast<int>(seconds + 0.5);
    char buf[32];
    if (total >= 3600) {
        std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", total / 3600, total / 60 % 60, total % 60);
    } else {
        std::snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
    }
    return buf;
}

static nlohmann::json listXattrs(const fs::path& path) {
    nlohmann::json names = nlohmann::json::array();
    ssize_t len = listxattr(path.c_str(), nullptr, 0);
    if (len <= 0) return names;

    std::string buf(static_cast<std::size_t>(len), '\0');
    len = listxattr(path.c_str(), buf.data(), buf.size());
    if (len <= 0) return names;

    // Names are NUL separated.
    for (std::size_t pos = 0; pos < static_cast<std::size_t>(len);) {
        std::string name(buf.c_str() + pos);
        if (!name.empty()) names.push_back(name);
        pos += name.size() + 1;
    }
    return names;
}

FileAnalysis::FileAnalysis(AnalysisLevel level)
    : level(level)
{
#ifdef DUPLEXHUNTER_HAS_FFMPEG
    analyzers.push_back(std::make_unique<MediaAnalyzer>());
#endif
    analyzers.push_back(std::make_unique<GenericAnalyzer>());
}

FileAnalysis::~FileAnalysis() {

}

nlohmann::json FileAnalysis::fileSystemStats(const fs::path& path) {
    nlohmann::json j;
    j["symlink"] = fs::is_symlink(path);

    struct statx st{};
    if (statx(AT_FDCWD, path.c_str(), 0, STATX_BASIC_STATS | STATX_BTIME, &st) != 0) {
        j["error"] = "Failed to stat file";
        return j;
    }

    char mode[8];
    std::snprintf(mode, sizeof(mode), "%04o", st.stx_mode & 07777);
    j["mode"] = mode;

    struct passwd* pw = getpwuid(st.stx_uid);
    struct group* gr = getgrgid(st.stx_gid);
    j["owner"] = pw ? nlohmann::json(pw->pw_name) : nlohmann::json(st.stx_uid);
    j["group"] = gr ? nlohmann::json(gr->gr_name) : nlohmann::json(st.stx_gid);

    j["inode"] = st.stx_ino;
    j["hardLinks"] = st.stx_nlink;
    j["sparse"] = st.stx_blocks * 512 < st.stx_size;

    j["modified"] = formatTime(st.stx_mtime.tv_sec);
    j["accessed"] = formatTime(st.stx_atime.tv_sec);
    j["changed"] = formatTime(st.stx_ctime.tv_sec);
    j["created"] = (st.stx_mask & STATX_BTIME) ? formatTime(st.stx_btime.tv_sec) : nlohmann::json(nullptr);

    j["xattrs"] = listXattrs(path);
    return j;
}

nlohmann::json FileAnalysis::externalSubtitles(const fs::path& video, double videoDuration) {
    nlohmann::json subtitles = subtitleFinder.subtitlesFor(video);
    if (level < AnalysisLevel::Packets) return subtitles;

    for (auto& subtitle : subtitles) {
        const fs::path path = subtitle["path"].get<std::string>();
        const std::string format = subtitle["format"];
        const bool text = format != "pgs" && format != "vobsub";
        nlohmann::json warnings = nlohmann::json::array();

        if (text) {
            std::string encoding = detector.detect(path).encoding;
            subtitle["encoding"] = encoding;
            if (!TEXT_ENCODINGS_OK.count(encoding)) {
                warnings.push_back("Not UTF-8 (" + encoding + "), accented letters may show garbled");
            }
        }

#ifdef DUPLEXHUNTER_HAS_FFMPEG
        nlohmann::json stats = MediaAnalyzer::subtitleStats(path);
        for (auto& [key, value] : stats.items()) subtitle[key] = value;

        if (stats.contains("error")) {
            warnings.push_back("Unreadable: " + stats["error"].get<std::string>());
        } else if (stats["cues"] == 0) {
            warnings.push_back("Contains no subtitles");
        } else if (videoDuration > 0 && stats["lastCue"].is_number()) {
            double lastCue = stats["lastCue"];
            if (lastCue > videoDuration + SUBTITLE_OVERRUN) {
                warnings.push_back("Ends " + formatDuration(lastCue - videoDuration) + " after the video");
            } else if (!subtitle["forced"].get<bool>() && lastCue < videoDuration * SUBTITLE_MIN_COVERAGE) {
                // Forced subtitles only cover foreign language parts, so they may end early.
                warnings.push_back("Ends at " + formatDuration(lastCue) + " of a " + formatDuration(videoDuration) + " video");
            }
        }
#endif
        subtitle["warnings"] = warnings;
    }
    return subtitles;
}

nlohmann::json FileAnalysis::analyze(const FileMatchGroup& group) {
    nlohmann::json j;
    j["size"] = group.size;
    j["hash"] = group.hash.has_value() ? nlohmann::json(*group.hash) : nlohmann::json(nullptr);
    j["duplicate"] = group.files.size() > 1;

    const fs::path representative = group.files.front();
    FileType type = detector.detect(representative);

    nlohmann::json content;
    content["type"] = {
        {"mime", type.mime},
        {"encoding", type.encoding},
        {"description", type.description},
        {"extensions", type.extensions}
    };

    for (auto& analyzer : analyzers) {
        if (!analyzer->canHandle(type)) continue;
        try {
            content[analyzer->name()] = analyzer->analyze(representative, type, level);
        } catch (const std::exception& e) {
            content[analyzer->name()] = { {"error", e.what()} };
        }
    }
    j["content"] = content;

    // Media FFmpeg could not open is still treated as video by its MIME type.
    const nlohmann::json media = content.value("media", nlohmann::json::object());
    const bool video = media.value("kind", "") == "video" || type.mime.rfind("video/", 0) == 0;
    const double videoDuration = media.contains("duration") && media["duration"].is_number()
        ? media["duration"].get<double>() : 0.0;

    nlohmann::json files = nlohmann::json::array();
    for (const std::string& file : group.files) {
        nlohmann::json entry = fileSystemStats(file);
        entry["path"] = file;
        // Unknown when libmagic has no extension list for the detected content.
        entry["extensionMatchesContent"] = type.extensions.empty()
            ? nlohmann::json(nullptr)
            : nlohmann::json(FileTypeDetector::extensionMatches(file, type));

        // Subtitle files depend on where each copy is stored, so they are found per file.
        if (video) {
            entry["subtitles"] = externalSubtitles(file, videoDuration);
        }
        if (SubtitleFinder::isSubtitle(file)) {
            std::optional<fs::path> owner = subtitleFinder.videoFor(file);
            entry["subtitleFor"] = owner ? nlohmann::json(owner->string()) : nlohmann::json(nullptr);
        }
        files.push_back(entry);
    }
    j["files"] = files;

    return j;
}
