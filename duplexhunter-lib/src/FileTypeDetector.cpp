#include "FileTypeDetector.hpp"

#include <magic.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

// Extensions for common types the libmagic database has no extension list for.
static const std::unordered_map<std::string, std::vector<std::string>> FALLBACK_EXTENSIONS = {
    { "audio/mpeg",                   { "mp3", "mp2", "mpga" } },
    { "audio/flac",                   { "flac" } },
    { "audio/x-flac",                 { "flac" } },
    { "audio/x-wav",                  { "wav" } },
    { "audio/ogg",                    { "ogg", "oga", "opus" } },
    { "application/ogg",              { "ogg", "oga", "ogv", "opus" } },
    { "video/x-matroska",             { "mkv", "mka", "mk3d", "webm" } },
    { "video/webm",                   { "webm" } },
    { "video/quicktime",              { "mov", "qt" } },
    { "video/x-msvideo",              { "avi" } },
    { "video/MP2T",                   { "ts", "m2ts", "mts" } },
    { "image/tiff",                   { "tif", "tiff" } },
    { "image/heic",                   { "heic", "heif" } },
    { "application/zip",              { "zip", "jar", "apk", "docx", "xlsx", "pptx", "odt", "ods", "odp",
                                        "epub", "cbz", "xpi", "whl", "nupkg", "ipa", "kmz", "3mf" } },
    { "application/gzip",             { "gz", "tgz" } },
    { "application/x-xz",             { "xz", "txz" } },
    { "application/x-bzip2",          { "bz2", "tbz2" } },
    { "application/x-tar",            { "tar" } },
    { "application/x-7z-compressed",  { "7z" } },
    { "application/x-rar",            { "rar" } },
    { "application/json",             { "json" } },
    { "text/html",                    { "html", "htm" } },
};

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

FileTypeDetector::FileTypeDetector() {
    cookie = magic_open(MAGIC_NONE);
    if (!cookie) {
        throw std::runtime_error("Failed to initialize libmagic");
    }
    if (magic_load(cookie, nullptr) != 0) {
        std::string err = magic_error(cookie);
        magic_close(cookie);
        throw std::runtime_error("Failed to load libmagic database: " + err);
    }
}

FileTypeDetector::~FileTypeDetector() {
    magic_close(cookie);
}

std::string FileTypeDetector::query(const fs::path& path, int flags) {
    magic_setflags(cookie, flags);
    const char* result = magic_file(cookie, path.c_str());
    return result ? std::string(result) : std::string();
}

FileType FileTypeDetector::detect(const fs::path& path) {
    FileType type;
    type.mime = query(path, MAGIC_MIME_TYPE);
    type.encoding = query(path, MAGIC_MIME_ENCODING);
    type.description = query(path, MAGIC_NONE);

    // libmagic returns a '/' separated list of extensions, or "???" when unknown.
    std::string extensions = query(path, MAGIC_EXTENSION);
    if (!extensions.empty() && extensions != "???") {
        std::stringstream ss(extensions);
        std::string ext;
        while (std::getline(ss, ext, '/')) {
            if (!ext.empty()) type.extensions.push_back(toLower(ext));
        }
    } else {
        auto it = FALLBACK_EXTENSIONS.find(type.mime);
        if (it != FALLBACK_EXTENSIONS.end()) type.extensions = it->second;
    }
    return type;
}

bool FileTypeDetector::extensionMatches(const fs::path& path, const FileType& type) {
    std::string ext = path.extension().string();
    if (!ext.empty() && ext.front() == '.') ext.erase(0, 1);
    ext = toLower(ext);
    return std::find(type.extensions.begin(), type.extensions.end(), ext) != type.extensions.end();
}
