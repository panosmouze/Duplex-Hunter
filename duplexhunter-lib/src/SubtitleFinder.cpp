#include "SubtitleFinder.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace {

const std::set<std::string> SUBTITLE_EXTENSIONS = { "srt", "ass", "ssa", "vtt", "sub", "idx", "sup" };

const std::set<std::string> VIDEO_EXTENSIONS = {
    "mkv", "mp4", "m4v", "avi", "mov", "wmv", "webm", "ts", "m2ts", "mts",
    "mpg", "mpeg", "flv", "ogv", "3gp", "divx", "vob"
};

// Folder names commonly used for subtitles of a release.
const std::set<std::string> SUBTITLE_FOLDERS = { "subs", "sub", "subtitles", "subtitle" };

struct Language {
    const char* code;
    std::vector<const char*> names;
};

// ISO 639-1 code, followed by the ISO 639-2 codes and English name used in file names.
// "hi" is deliberately not Hindi: in subtitle names it marks hearing impaired subtitles.
const std::vector<Language> LANGUAGES = {
    { "en", { "en", "eng", "english" } },
    { "el", { "el", "gre", "ell", "greek" } },
    { "fr", { "fr", "fre", "fra", "french" } },
    { "de", { "de", "ger", "deu", "german" } },
    { "es", { "es", "spa", "spanish" } },
    { "it", { "it", "ita", "italian" } },
    { "pt", { "pt", "por", "portuguese" } },
    { "nl", { "nl", "dut", "nld", "dutch" } },
    { "ru", { "ru", "rus", "russian" } },
    { "pl", { "pl", "pol", "polish" } },
    { "tr", { "tr", "tur", "turkish" } },
    { "ar", { "ar", "ara", "arabic" } },
    { "he", { "he", "heb", "hebrew" } },
    { "zh", { "zh", "chi", "zho", "chinese" } },
    { "ja", { "ja", "jpn", "japanese" } },
    { "ko", { "ko", "kor", "korean" } },
    { "sv", { "sv", "swe", "swedish" } },
    { "no", { "no", "nor", "nob", "norwegian" } },
    { "da", { "da", "dan", "danish" } },
    { "fi", { "fi", "fin", "finnish" } },
    { "cs", { "cs", "cze", "ces", "czech" } },
    { "hu", { "hu", "hun", "hungarian" } },
    { "ro", { "ro", "rum", "ron", "romanian" } },
    { "bg", { "bg", "bul", "bulgarian" } },
    { "hr", { "hr", "hrv", "croatian" } },
    { "sr", { "sr", "srp", "serbian" } },
    { "uk", { "uk", "ukr", "ukrainian" } },
    { "hi", { "hin", "hindi" } },
    { "id", { "ind", "indonesian" } },
    { "vi", { "vi", "vie", "vietnamese" } },
    { "th", { "th", "tha", "thai" } },
};

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string extensionOf(const fs::path& path) {
    std::string ext = path.extension().string();
    if (!ext.empty()) ext.erase(0, 1);
    return toLower(ext);
}

const char* languageCode(const std::string& token) {
    for (const Language& language : LANGUAGES) {
        for (const char* name : language.names) {
            if (token == name) return language.code;
        }
    }
    return nullptr;
}

std::vector<std::string> splitTokens(const std::string& s, const std::string& separators) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : s) {
        if (separators.find(c) != std::string::npos) {
            if (!current.empty()) tokens.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

// "Movie" matches "movie" and "movie.en.forced", but not "movie 2".
bool stemMatches(const std::string& subtitleStem, const std::string& videoStem) {
    std::string sub = toLower(subtitleStem);
    std::string video = toLower(videoStem);
    return sub == video || (sub.size() > video.size() && sub.compare(0, video.size(), video) == 0 && sub[video.size()] == '.');
}

std::string formatOf(const fs::path& path, bool hasIdx) {
    std::string ext = extensionOf(path);
    if (ext == "srt") return "subrip";
    if (ext == "ass" || ext == "ssa") return "ass";
    if (ext == "vtt") return "webvtt";
    if (ext == "idx") return "vobsub";
    if (ext == "sub") return hasIdx ? "vobsub" : "microdvd";
    if (ext == "sup") return "pgs";
    return ext;
}

} // namespace

bool SubtitleFinder::isSubtitle(const fs::path& path) {
    return SUBTITLE_EXTENSIONS.count(extensionOf(path)) > 0;
}

bool SubtitleFinder::isVideo(const fs::path& path) {
    return VIDEO_EXTENSIONS.count(extensionOf(path)) > 0;
}

const SubtitleFinder::Listing& SubtitleFinder::list(const fs::path& dir) {
    auto it = listings.find(dir.string());
    if (it != listings.end()) return it->second;

    Listing listing;
    std::error_code ec;
    for (fs::directory_iterator entry(dir, ec), end; !ec && entry != end; entry.increment(ec)) {
        std::error_code typeError;
        if (entry->is_regular_file(typeError)) {
            listing.files.push_back(entry->path());
        } else if (entry->is_directory(typeError)) {
            listing.dirs.push_back(entry->path());
        }
    }
    std::sort(listing.files.begin(), listing.files.end());
    return listings.emplace(dir.string(), std::move(listing)).first->second;
}

std::vector<fs::path> SubtitleFinder::videosIn(const fs::path& dir) {
    std::vector<fs::path> videos;
    for (const fs::path& file : list(dir).files) {
        if (isVideo(file)) videos.push_back(file);
    }
    return videos;
}

nlohmann::json SubtitleFinder::describe(const fs::path& subtitle, const std::string& nameTags) {
    nlohmann::json j;
    j["path"] = subtitle.string();

    bool hasIdx = false;
    if (extensionOf(subtitle) == "sub") {
        fs::path idx = subtitle;
        idx.replace_extension(".idx");
        std::error_code ec;
        hasIdx = fs::exists(idx, ec);
    }
    j["format"] = formatOf(subtitle, hasIdx);

    nlohmann::json language = nullptr;
    bool forced = false, sdh = false;
    nlohmann::json otherTags = nlohmann::json::array();

    for (const std::string& raw : splitTokens(nameTags, "._- ")) {
        std::string token = toLower(raw);
        if (token == "forced") {
            forced = true;
        } else if (token == "sdh" || token == "cc" || token == "hi") {
            sdh = true;
        } else if (const char* code = languageCode(token); code && language.is_null()) {
            language = code;
        } else if (!std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isdigit(c); })) {
            // Numbers are only ordering prefixes, as in "2_English".
            otherTags.push_back(raw);
        }
    }

    j["language"] = language;
    j["forced"] = forced;
    j["sdh"] = sdh;
    j["tags"] = otherTags;
    return j;
}

nlohmann::json SubtitleFinder::subtitlesFor(const fs::path& video) {
    nlohmann::json subtitles = nlohmann::json::array();
    const fs::path dir = video.parent_path();
    const std::string videoStem = video.stem().string();

    auto add = [&](const fs::path& file, const std::string& nameTags) {
        if (!isSubtitle(file)) return;
        // A VobSub .sub is described by its .idx.
        if (extensionOf(file) == "sub") {
            fs::path idx = file;
            idx.replace_extension(".idx");
            std::error_code ec;
            if (fs::exists(idx, ec)) return;
        }
        subtitles.push_back(describe(file, nameTags));
    };

    // Next to the video, e.g. Movie.en.forced.srt.
    for (const fs::path& file : list(dir).files) {
        std::string stem = file.stem().string();
        if (stemMatches(stem, videoStem)) add(file, stem.substr(videoStem.size()));
    }

    // In a subtitles folder: shared by the folder's only video, or in a subfolder named after the video.
    bool onlyVideo = videosIn(dir).size() == 1;
    for (const fs::path& subDir : list(dir).dirs) {
        if (!SUBTITLE_FOLDERS.count(toLower(subDir.filename().string()))) continue;

        if (onlyVideo) {
            for (const fs::path& file : list(subDir).files) add(file, file.stem().string());
        }
        for (const fs::path& nested : list(subDir).dirs) {
            if (toLower(nested.filename().string()) != toLower(videoStem)) continue;
            for (const fs::path& file : list(nested).files) add(file, file.stem().string());
        }
    }
    return subtitles;
}

std::optional<fs::path> SubtitleFinder::videoFor(const fs::path& subtitle) {
    const fs::path dir = subtitle.parent_path();
    const std::string stem = subtitle.stem().string();

    // Next to the video; the longest matching name wins, e.g. "Movie.Part2" over "Movie".
    std::optional<fs::path> best;
    for (const fs::path& video : videosIn(dir)) {
        std::string videoStem = video.stem().string();
        if (stemMatches(stem, videoStem) && (!best || videoStem.size() > best->stem().string().size())) {
            best = video;
        }
    }
    if (best) return best;

    // Subs/2_English.srt belongs to the only video of the parent folder.
    if (SUBTITLE_FOLDERS.count(toLower(dir.filename().string()))) {
        std::vector<fs::path> videos = videosIn(dir.parent_path());
        if (videos.size() == 1) return videos.front();
    }

    // Subs/Episode/2_English.srt belongs to Episode.mkv.
    const fs::path parent = dir.parent_path();
    if (SUBTITLE_FOLDERS.count(toLower(parent.filename().string()))) {
        for (const fs::path& video : videosIn(parent.parent_path())) {
            if (toLower(video.stem().string()) == toLower(dir.filename().string())) return video;
        }
    }
    return std::nullopt;
}
